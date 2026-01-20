/**
 * @file cloud_manager.hpp
 * @brief Cloud connectivity manager
 *
 * Orchestrates authentication, telemetry upload, and command polling.
 * Entry point for all cloud operations from the application.
 *
 * Emits events via CLOUD_EVENTS for:
 * - Authentication status changes
 * - Device revocation
 * - Commands (reboot, factory_reset)
 */

#pragma once

#include "cloud_client.hpp"
#include "command_handler.hpp"
#include "command_service.hpp"
#include "config.hpp"
#include "credentials.hpp"
#include "device_auth.hpp"
#include "events.hpp"
#include "measurement_serializer.hpp"
#include "telemetry_service.hpp"

#include <core/event_loop.hpp>
#include <core/result.hpp>
#include <core/rtc_storage.hpp>
#include <core/storage.hpp>
#include <core/timer.hpp>
#include <proto/measurement_adapter.hpp>
#include <sensor/measurement.hpp>

#include <esp_log.h>

#include <chrono>
#include <memory>
#include <span>

namespace cloud {

/// Cloud manager state
enum class CloudState : uint8_t {
  Uninitialized,
  NoCredentials,
  Connecting,
  Authenticated,
  Revoked,
  Error,
};

/// Cloud manager configuration
struct CloudManagerConfig {
  std::chrono::minutes telemetry_interval{5};
  std::chrono::minutes command_poll_interval{1};
  bool skip_cert_verify{false};
};

/// Cloud connectivity manager
///
/// Usage:
/// 1. Call init() once at startup
/// 2. Call start() when WiFi connects
/// 3. Call stop() when WiFi disconnects
/// 4. Call send_telemetry() to upload measurements immediately
///
/// @thread_safety Not thread-safe. Call from main task only.
class CloudManager {
public:
  CloudManager(core::IStorage &creds_storage, core::RtcAuthToken &rtc_token,
               const CloudManagerConfig &config = {})
      : creds_storage_(creds_storage), rtc_token_(rtc_token), config_(config) {}

  ~CloudManager() { stop(); }

  CloudManager(const CloudManager &) = delete;
  CloudManager &operator=(const CloudManager &) = delete;
  CloudManager(CloudManager &&) = delete;
  CloudManager &operator=(CloudManager &&) = delete;

  /// Initialize cloud services (call once at startup)
  [[nodiscard]] core::Status init() {
    // Load credentials from NVS
    auto creds_result = load_credentials(creds_storage_);
    if (!creds_result) {
      ESP_LOGW(TAG, "No device credentials found");
      state_ = CloudState::NoCredentials;
      return core::Err(ESP_ERR_NOT_FOUND);
    }

    credentials_ = *creds_result;
    ESP_LOGI(TAG, "Device: %s", credentials_.device_id.data());

    // Build cloud config
    cloud_config_ = CloudConfig{
        .skip_cert_verify = config_.skip_cert_verify,
    };

    // Create auth provider
    auth_.emplace(credentials_, &rtc_token_, cloud_config_);

    // Create client
    client_.emplace(*auth_, cloud_config_);

    // Create services
    command_service_.emplace(*client_);
    telemetry_service_.emplace(*client_, serializer_);

    state_ = CloudState::Uninitialized;
    ESP_LOGI(TAG, "Cloud manager initialized");
    return core::Ok();
  }

  /// Start cloud services (call when WiFi connected)
  [[nodiscard]] core::Status start() {
    if (!auth_ || !client_) {
      ESP_LOGE(TAG, "Not initialized");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (state_ == CloudState::Revoked) {
      ESP_LOGE(TAG, "Device revoked, cannot start");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    ESP_LOGI(TAG, "Starting cloud services...");
    state_ = CloudState::Connecting;

    // Initialize transport
    if (auto status = client_->init(); !status) {
      ESP_LOGE(TAG, "Client init failed");
      state_ = CloudState::Error;
      return status;
    }

    // Authenticate
    auto auth_result = auth_->authenticate();
    if (auth_result.state == AuthState::Revoked) {
      state_ = CloudState::Revoked;
      on_revoked();
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (auth_result.state != AuthState::Authenticated) {
      ESP_LOGE(TAG, "Authentication failed: %d",
               static_cast<int>(auth_result.error));
      state_ = CloudState::Error;
      core::events().publish(CLOUD_EVENTS, CloudEvent::AuthFailed);
      return core::Err(ESP_FAIL);
    }

    state_ = CloudState::Authenticated;
    ESP_LOGI(TAG, "Authenticated with cloud");
    core::events().publish(CLOUD_EVENTS, CloudEvent::Authenticated);

    // Start periodic timers
    start_timers();

    return core::Ok();
  }

  /// Stop cloud services (call when WiFi disconnects)
  void stop() {
    stop_timers();

    if (client_) {
      client_->disconnect();
    }

    if (state_ != CloudState::Revoked) {
      state_ = CloudState::Uninitialized;
    }

    ESP_LOGI(TAG, "Cloud services stopped");
  }

  /// Send telemetry now (called from timer or manually)
  [[nodiscard]] TelemetryResult
  send_telemetry(std::span<const sensor::Measurement> measurements) {
    if (!telemetry_service_ || state_ != CloudState::Authenticated) {
      return {.error = CloudError::NotAuthenticated};
    }

    if (measurements.empty()) {
      return {.success = true};
    }

    ESP_LOGI(TAG, "Sending %zu measurements", measurements.size());
    auto result = telemetry_service_->send(measurements);

    if (result.success) {
      ESP_LOGI(TAG, "Telemetry sent OK");
    } else {
      ESP_LOGW(TAG, "Telemetry failed: %d", static_cast<int>(result.error));
      handle_error(result.error);
    }

    return result;
  }

  /// Poll and process commands now
  void poll_commands() {
    if (!command_service_ || state_ != CloudState::Authenticated) {
      return;
    }

    CommandBuffer cmd_buffer;
    auto result = command_service_->poll(cmd_buffer);

    if (!result.success) {
      ESP_LOGW(TAG, "Command poll failed: %d", static_cast<int>(result.error));
      handle_error(result.error);
      return;
    }

    if (cmd_buffer.empty()) {
      return;
    }

    ESP_LOGI(TAG, "Processing %zu commands", cmd_buffer.size());
    command_handler_.process_all(*command_service_, cmd_buffer);
  }

  /// Register command handler callback
  void on_command(CommandType type, CommandHandlerFn handler) {
    command_handler_.register_handler(type, std::move(handler));
  }

  /// Send device info (app_name, firmware_version) to backend
  /// Call after successful authentication
  [[nodiscard]] bool send_device_info(std::string_view app_name,
                                      std::string_view firmware_version) {
    if (!client_ || state_ != CloudState::Authenticated) {
      return false;
    }

    // Build JSON body
    std::array<char, 256> json_buffer{};
    int len = snprintf(json_buffer.data(), json_buffer.size(),
                       R"({"app_name":"%.*s","app_version":"%.*s"})",
                       static_cast<int>(app_name.size()), app_name.data(),
                       static_cast<int>(firmware_version.size()),
                       firmware_version.data());

    if (len < 0 || static_cast<size_t>(len) >= json_buffer.size()) {
      ESP_LOGE(TAG, "Device info JSON too large");
      return false;
    }

    auto response =
        client_->put(endpoints::DEVICE_INFO,
                     std::span<const uint8_t>(
                         reinterpret_cast<const uint8_t *>(json_buffer.data()),
                         static_cast<size_t>(len)),
                     transport::ContentType::Json);

    if (!response.success) {
      ESP_LOGW(TAG, "Device info update failed: %d",
               static_cast<int>(response.error));
      return false;
    }

    ESP_LOGI(TAG, "Device info sent: %.*s v%.*s",
             static_cast<int>(app_name.size()), app_name.data(),
             static_cast<int>(firmware_version.size()),
             firmware_version.data());
    return true;
  }

  [[nodiscard]] CloudState state() const { return state_; }
  [[nodiscard]] bool is_connected() const {
    return state_ == CloudState::Authenticated;
  }
  [[nodiscard]] bool is_revoked() const {
    return state_ == CloudState::Revoked;
  }

private:
  static constexpr const char *TAG = "CloudMgr";

  void start_timers() {
    // Telemetry timer
    telemetry_timer_ = std::make_unique<core::PeriodicTimer>(
        [this]() { on_telemetry_tick(); });
    (void)telemetry_timer_->start(config_.telemetry_interval);

    // Command poll timer
    command_timer_ =
        std::make_unique<core::PeriodicTimer>([this]() { poll_commands(); });
    (void)command_timer_->start(config_.command_poll_interval);

    // Token refresh timer - runs every minute, checks if refresh needed
    token_refresh_timer_ = std::make_unique<core::PeriodicTimer>(
        [this]() { check_token_refresh(); });
    (void)token_refresh_timer_->start(std::chrono::minutes(1));

    ESP_LOGI(
        TAG,
        "Timers started: telemetry=%dmin, commands=%dmin, token_check=1min",
        static_cast<int>(config_.telemetry_interval.count()),
        static_cast<int>(config_.command_poll_interval.count()));
  }

  void stop_timers() {
    if (telemetry_timer_) {
      telemetry_timer_->stop();
      telemetry_timer_.reset();
    }
    if (command_timer_) {
      command_timer_->stop();
      command_timer_.reset();
    }
    if (token_refresh_timer_) {
      token_refresh_timer_->stop();
      token_refresh_timer_.reset();
    }
  }

  void on_telemetry_tick() {
    // Timer tick - application handles actual telemetry via send_telemetry()
    // This could emit an event if needed for app to respond to
  }

  void check_token_refresh() {
    if (!auth_ || state_ != CloudState::Authenticated) {
      return;
    }

    if (!auth_->needs_refresh()) {
      return;
    }

    ESP_LOGI(TAG, "Token needs refresh, refreshing...");
    auto status = auth_->refresh();

    if (!status) {
      ESP_LOGW(TAG, "Token refresh failed: %s",
               esp_err_to_name(status.error()));
      if (auth_->is_revoked()) {
        state_ = CloudState::Revoked;
        on_revoked();
      }
    } else {
      ESP_LOGI(TAG, "Token refreshed successfully");
    }
  }

  void handle_error(CloudError error) {
    if (error == CloudError::DeviceRevoked) {
      state_ = CloudState::Revoked;
      on_revoked();
    }
  }

  void on_revoked() {
    ESP_LOGE(TAG, "Device has been revoked!");
    stop_timers();
    core::events().publish(CLOUD_EVENTS, CloudEvent::Revoked);
  }

  // Dependencies
  core::IStorage &creds_storage_;
  core::RtcAuthToken &rtc_token_;
  CloudManagerConfig config_;

  // State
  CloudState state_{CloudState::Uninitialized};
  DeviceCredentials credentials_{};
  CloudConfig cloud_config_{};

  // Services (optional for deferred init)
  std::optional<DeviceAuthProvider> auth_;
  std::optional<CloudClient> client_;
  std::optional<CommandService> command_service_;

  // Telemetry
  MeasurementSerializer serializer_;
  std::optional<TelemetryService<sensor::Measurement, proto::MAX_BATCH_SIZE>>
      telemetry_service_;

  // Commands
  CommandHandler command_handler_;

  // Timers
  std::unique_ptr<core::PeriodicTimer> telemetry_timer_;
  std::unique_ptr<core::PeriodicTimer> command_timer_;
  std::unique_ptr<core::PeriodicTimer> token_refresh_timer_;
};

} // namespace cloud
