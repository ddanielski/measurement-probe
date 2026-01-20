/**
 * @file app.cpp
 * @brief Measurement probe application implementation
 */

#include <application/app.hpp>
#include <sensor/log.hpp>

#include "app_config.hpp"

#include <esp_app_desc.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <span>

namespace {

/// RTC memory for auth token
RTC_DATA_ATTR core::RtcAuthToken g_rtc_auth_token;

} // namespace

namespace application {

MeasurementProbe::MeasurementProbe(Board &board,
                                   std::chrono::seconds sleep_interval)
    : board_(board), sleep_(sleep_interval) {}

void MeasurementProbe::run() {
  log_boot_info();
  track_boot_count();

  if (!board_.valid()) {
    ESP_LOGE(TAG, "Board not valid, halting");
    return;
  }

  init_wifi();
  init_sensors();
  init_cloud();

  // All subsystems initialized
  core::events().publish(core::APP_EVENTS, core::AppEvent::StartupComplete);
  ESP_LOGI(TAG, "Startup complete");

  run_continuous_mode();
}

void MeasurementProbe::log_boot_info() {
  const auto *app_desc = esp_app_get_description();
  auto wake = power::get_wake_reason();
  ESP_LOGI(TAG, "%s v%s | Wake: %s", app_desc->project_name, app_desc->version,
           power::to_string(wake));
}

void MeasurementProbe::track_boot_count() {
  auto &app_storage = storage(core::NamespaceId::App);
  auto guard = app_storage.auto_commit();
  uint32_t boots = app_storage.get<uint32_t>("boots").value_or(0) + 1;
  if (app_storage.set<uint32_t>("boots", boots)) {
    ESP_LOGI(TAG, "Boot #%" PRIu32, boots);
  }
}

void MeasurementProbe::init_wifi() {
  // Configure WiFi manager
  network::WifiConfig wifi_config{
      .max_retries = app::config::WIFI_MAX_RETRIES,
      .initial_backoff_ms = 1000,
      .max_backoff_ms = 30000,
  };

  // Initialize WiFi with storage from WiFi namespace
  auto &wifi_storage = storage(core::NamespaceId::Wifi);
  if (auto err = wifi_.init(wifi_storage, wifi_config); !err) {
    ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err.error()));
    return;
  }

  // Subscribe to network events (for deferred cloud start/stop)
  network_event_sub_ = core::events().subscribe(
      NETWORK_EVENTS, ESP_EVENT_ANY_ID, network_event_handler, this);

  // Set up state change callback (for logging/UI only - no heavy work)
  wifi_.on_state_change(
      [this](network::WifiState old_state, network::WifiState new_state) {
        on_wifi_state_change(old_state, new_state);
      });

  // Check for stored credentials
  if (wifi_.has_credentials()) {
    ESP_LOGI(TAG, "Found stored WiFi credentials, connecting...");
    if (auto err = wifi_.connect(); !err) {
      ESP_LOGW(TAG, "Connect failed: %s", esp_err_to_name(err.error()));
    }
  } else {
    ESP_LOGI(TAG, "No stored credentials, starting BLE provisioning...");

    network::ProvisioningConfig prov_config{
        .device_name_prefix = PROVISIONING_DEVICE_NAME,
        .pop = PROVISIONING_POP,
        .service_uuid = nullptr,
        .timeout_sec = PROVISIONING_TIMEOUT_SEC,
    };

    if (auto err = wifi_.start_provisioning(prov_config); !err) {
      ESP_LOGE(TAG, "Provisioning start failed: %s",
               esp_err_to_name(err.error()));
    }
  }
}

void MeasurementProbe::on_wifi_state_change(network::WifiState old_state,
                                            network::WifiState new_state) {
  ESP_LOGI(TAG, "WiFi state: %d -> %d", static_cast<int>(old_state),
           static_cast<int>(new_state));

  switch (new_state) {
  case network::WifiState::Connected: {
    auto info = wifi_.connection_info();
    ESP_LOGI(TAG, "Connected! IP: %d.%d.%d.%d, RSSI: %d dBm", info.ip[0],
             info.ip[1], info.ip[2], info.ip[3], info.rssi);
    // Cloud start is deferred to main task via network_event_handler
    break;
  }
  case network::WifiState::Disconnected:
    ESP_LOGW(TAG, "WiFi disconnected");
    // Cloud stop is deferred to main task
    cloud_stop_pending_ = true;
    break;
  case network::WifiState::Provisioning:
    ESP_LOGI(TAG, "BLE provisioning active - use app to configure WiFi");
    break;
  case network::WifiState::Failed:
    ESP_LOGE(TAG, "WiFi connection failed after max retries");
    break;
  default:
    break;
  }
}

void MeasurementProbe::network_event_handler(void *arg,
                                             esp_event_base_t /*base*/,
                                             int32_t event_id,
                                             void * /*event_data*/) {
  auto *self = static_cast<MeasurementProbe *>(arg);
  auto event = static_cast<network::NetworkEvent>(event_id);

  // Just set flags - actual work happens on main task
  switch (event) {
  case network::NetworkEvent::WifiConnected:
    ESP_LOGI(TAG, "Network event: WiFi connected - scheduling cloud start");
    self->cloud_start_pending_ = true;
    break;
  case network::NetworkEvent::WifiDisconnected:
    ESP_LOGI(TAG, "Network event: WiFi disconnected - scheduling cloud stop");
    self->cloud_stop_pending_ = true;
    break;
  default:
    break;
  }
}

void MeasurementProbe::init_sensors() {
  // Subscribe to sensor data events (event-driven architecture)
  sensor_event_sub_ =
      core::events().subscribe(SENSOR_EVENTS, sensor::SensorEvent::DataReady,
                               sensor_event_handler, this);

  // Create and register timestamp sensor with 1s interval
  timestamp_monitor_.emplace(std::chrono::seconds(1));
  sensors_.register_monitor(*timestamp_monitor_);

  // Create and register BME680 with externally-timed monitor (BSEC controls
  // timing)
  auto &bsec_storage = storage(core::NamespaceId::Bsec);
  bme680_monitor_.emplace(board_.i2c(), bsec_storage,
                          sensor::bme680::BME680Sensor::Config{
                              .address = app::config::BME680_ADDRESS,
                              .sensor_id = static_cast<sensor::SensorIdType>(
                                  sensor::SensorId::BME680)});
  sensors_.register_monitor(*bme680_monitor_);

  ESP_LOGI(TAG, "Registered %zu sensor monitor(s)", sensors_.monitor_count());

  // Set up periodic logging timer (aggregates multiple sensor readings)
  log_timer_ =
      std::make_unique<core::PeriodicTimer>([this]() { on_log_timer(); });
  [[maybe_unused]] auto status = log_timer_->start(std::chrono::seconds(10));
}

void MeasurementProbe::sensor_event_handler(void *arg,
                                            esp_event_base_t /*base*/,
                                            int32_t event_id,
                                            void *event_data) {
  auto *self = static_cast<MeasurementProbe *>(arg);
  if (event_id == static_cast<int32_t>(sensor::SensorEvent::DataReady) &&
      event_data != nullptr) {
    // ESP-IDF copies event data, so we need to reconstruct it
    const auto *event =
        static_cast<const sensor::SensorDataEvent *>(event_data);
    self->on_sensor_data(*event);
  }
}

void MeasurementProbe::on_sensor_data(const sensor::SensorDataEvent &event) {
  ESP_LOGD(TAG, "Sensor %u: %zu measurements ready", event.sensor_id,
           event.count);
}

void MeasurementProbe::on_log_timer() {
  // Zero-allocation read into stack buffer
  std::array<sensor::Measurement, MAX_MEASUREMENTS> buffer{};
  size_t count = sensors_.read_all_into(buffer);
  sensor::log_measurements(TAG, std::span(buffer.data(), count));

  // Also send to cloud if connected
  send_telemetry();
}

void MeasurementProbe::run_continuous_mode() {
  ESP_LOGI(TAG, "Running - monitors sample independently, logging every 10s");

  // Main loop - processes deferred operations on main task (large stack)
  while (true) {
    // Handle deferred cloud start (triggered by network event)
    if (cloud_start_pending_.exchange(false)) {
      ESP_LOGI(TAG, "Executing deferred cloud start on main task");
      start_cloud();
    }

    // Handle deferred cloud stop (triggered by network event)
    if (cloud_stop_pending_.exchange(false)) {
      ESP_LOGI(TAG, "Executing deferred cloud stop on main task");
      if (cloud_) {
        cloud_->stop();
      }
    }

    // Handle deferred device info update (triggered after auth)
    if (device_info_pending_.exchange(false)) {
      ESP_LOGI(TAG, "Sending device info to backend");
      if (cloud_) {
        const auto *app_desc = esp_app_get_description();
        (void)cloud_->send_device_info(app_desc->project_name,
                                       app_desc->version);
      }
    }

    // Sleep briefly, then check again
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void MeasurementProbe::init_cloud() {
  auto &creds_storage = storage(core::NamespaceId::Cloud);

  // Check if device is provisioned
  if (!cloud::is_provisioned(creds_storage)) {
    ESP_LOGW(TAG, "Device not provisioned - cloud disabled");
    return;
  }

  // Subscribe to cloud events
  cloud_event_sub_ = core::events().subscribe(
      cloud::CLOUD_EVENTS, ESP_EVENT_ANY_ID, cloud_event_handler, this);

  cloud::CloudManagerConfig cloud_config{
      .telemetry_interval =
          std::chrono::minutes(app::config::cloud::TELEMETRY_INTERVAL_MIN),
      .command_poll_interval =
          std::chrono::minutes(app::config::cloud::COMMAND_POLL_INTERVAL_MIN),
      .skip_cert_verify = app::config::cloud::SKIP_CERT_VERIFY,
  };

  cloud_.emplace(creds_storage, g_rtc_auth_token, cloud_config);

  if (auto status = cloud_->init(); !status) {
    ESP_LOGE(TAG, "Cloud init failed: %s", esp_err_to_name(status.error()));
    cloud_.reset();
    return;
  }

  ESP_LOGI(TAG, "Cloud services initialized");
}

void MeasurementProbe::start_cloud() {
  if (!cloud_) {
    return;
  }

  if (auto status = cloud_->start(); !status) {
    ESP_LOGE(TAG, "Cloud start failed: %s", esp_err_to_name(status.error()));
    return;
  }

  // Send initial telemetry
  send_telemetry();
}

void MeasurementProbe::send_telemetry() {
  if (!cloud_ || !cloud_->is_connected()) {
    return;
  }

  // Read all current measurements
  std::array<sensor::Measurement, MAX_MEASUREMENTS> buffer{};
  size_t count = sensors_.read_all_into(buffer);

  if (count == 0) {
    return;
  }

  auto result = cloud_->send_telemetry(std::span(buffer.data(), count));
  if (!result.success) {
    ESP_LOGW(TAG, "Telemetry send failed");
  }
}

void MeasurementProbe::cloud_event_handler(void *arg, esp_event_base_t /*base*/,
                                           int32_t event_id, void * /*data*/) {
  auto *self = static_cast<MeasurementProbe *>(arg);
  auto event = static_cast<cloud::CloudEvent>(event_id);

  switch (event) {
  case cloud::CloudEvent::Authenticated:
    ESP_LOGI(TAG, "Cloud authenticated - scheduling device info update");
    self->device_info_pending_ = true;
    break;

  case cloud::CloudEvent::Revoked:
    self->on_device_revoked();
    break;

  case cloud::CloudEvent::RebootRequested:
    ESP_LOGI(TAG, "Reboot requested via cloud command");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    break;

  case cloud::CloudEvent::FactoryResetRequested:
    ESP_LOGW(TAG, "Factory reset requested via cloud command");
    self->handle_factory_reset();
    break;

  default:
    break;
  }
}

void MeasurementProbe::handle_factory_reset() {
  // Use generic storage interface to erase all namespaces
  ESP_LOGW(TAG, "Erasing all storage...");

  // Erase each namespace we use
  auto &app_storage = storage(core::NamespaceId::App);
  (void)app_storage.erase_all();

  auto &wifi_storage = storage(core::NamespaceId::Wifi);
  (void)wifi_storage.erase_all();

  auto &bsec_storage = storage(core::NamespaceId::Bsec);
  (void)bsec_storage.erase_all();

  auto &cloud_storage = storage(core::NamespaceId::Cloud);
  (void)cloud_storage.erase_all();

  ESP_LOGI(TAG, "Factory reset complete, rebooting...");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
}

void MeasurementProbe::on_device_revoked() {
  ESP_LOGE(TAG, "Device has been revoked by server!");
  ESP_LOGE(TAG, "Entering safe mode - awaiting factory reset");

  // Stop all normal operations
  if (log_timer_) {
    log_timer_->stop();
  }

  // Could show LED pattern, enter low-power mode, etc.
}

} // namespace application
