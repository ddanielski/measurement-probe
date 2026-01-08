/**
 * @file wifi_manager.hpp
 * @brief WiFi connection manager with state machine and auto-reconnect
 */

#pragma once

#include "wifi_types.hpp"

#include <core/event_loop.hpp>
#include <core/result.hpp>
#include <core/storage.hpp>
#include <core/timer.hpp>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <atomic>
#include <functional>
#include <memory>

/// Network events base (for event bus subscriptions)
CORE_EVENT_DECLARE_BASE(NETWORK_EVENTS);

namespace network {

/// WiFi manager - handles connection lifecycle, credentials, and reconnection
class WifiManager {
public:
  using StateCallback = std::function<void(WifiState, WifiState)>;

  WifiManager() = default;
  ~WifiManager();

  WifiManager(const WifiManager &) = delete;
  WifiManager &operator=(const WifiManager &) = delete;
  WifiManager(WifiManager &&) = delete;
  WifiManager &operator=(WifiManager &&) = delete;

  /// Initialize WiFi subsystem
  /// @param storage Storage for credentials (Wifi namespace)
  /// @param config WiFi configuration
  [[nodiscard]] core::Status init(core::IStorage &storage,
                                  const WifiConfig &config = {});

  /// Start connection using stored credentials
  /// @return Error if no credentials stored or init not called
  [[nodiscard]] core::Status connect();

  /// Connect with specific credentials (stores them)
  [[nodiscard]] core::Status connect(const WifiCredentials &creds);

  /// Disconnect from WiFi
  [[nodiscard]] core::Status disconnect();

  /// Start BLE provisioning mode
  [[nodiscard]] core::Status
  start_provisioning(const ProvisioningConfig &config);

  /// Stop provisioning mode
  [[nodiscard]] core::Status stop_provisioning();

  /// Check if provisioning is active
  [[nodiscard]] bool is_provisioning() const {
    return state_ == WifiState::Provisioning;
  }

  /// Get current state
  [[nodiscard]] WifiState state() const { return state_.load(); }

  /// Check if connected
  [[nodiscard]] bool is_connected() const {
    return state_ == WifiState::Connected;
  }

  /// Get connection info (valid only when connected)
  [[nodiscard]] ConnectionInfo connection_info() const { return conn_info_; }

  /// Check if credentials are stored
  [[nodiscard]] bool has_credentials() const;

  /// Clear stored credentials
  [[nodiscard]] core::Status clear_credentials();

  /// Set state change callback
  void on_state_change(StateCallback callback) {
    state_callback_ = std::move(callback);
  }

  /// Get singleton instance (for event handlers)
  static WifiManager *instance() { return instance_; }

private:
  /// NVS keys for credentials
  static constexpr const char *kKeySsid = "ssid";
  static constexpr const char *kKeyPassword = "pass";

  /// Load credentials from storage
  [[nodiscard]] core::Result<WifiCredentials> load_credentials();

  /// Save credentials to storage
  [[nodiscard]] core::Status save_credentials(const WifiCredentials &creds);

  /// Start the actual connection attempt
  [[nodiscard]] core::Status start_connect(const WifiCredentials &creds);

  /// Calculate backoff delay for current retry
  [[nodiscard]] uint32_t calculate_backoff() const;

  /// Reset retry counter and backoff
  void reset_retry_state();

  /// Schedule a reconnection attempt with backoff (non-blocking)
  void schedule_reconnect(uint32_t backoff_ms);

  /// Timer callback for reconnection
  void on_reconnect_timer();

  /// Provisioning timeout handler
  void on_prov_timeout();

  /// Transition to new state
  void set_state(WifiState new_state);

  /// Handle WiFi events from ESP-IDF
  static void wifi_event_handler(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *event_data);

  /// Handle IP events from ESP-IDF
  static void ip_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data);

  /// Handle provisioning events
  static void prov_event_handler(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *event_data);

  // Members
  core::IStorage *storage_ = nullptr;
  WifiConfig config_{};
  esp_netif_t *netif_ = nullptr;

  std::atomic<WifiState> state_{WifiState::Idle};
  ConnectionInfo conn_info_{};

  WifiCredentials pending_creds_{};
  uint8_t retry_count_ = 0;
  StateCallback state_callback_;

  core::EventSubscription wifi_sub_;
  core::EventSubscription ip_sub_;
  core::EventSubscription prov_sub_;

  /// Timer for non-blocking reconnection with backoff
  /// Uses unique_ptr for lazy initialization (timer is non-movable)
  std::unique_ptr<core::OneShotTimer> reconnect_timer_;

  /// Timer for provisioning timeout
  std::unique_ptr<core::OneShotTimer> prov_timeout_timer_;

  bool initialized_ = false;

  static WifiManager *instance_;
};

} // namespace network
