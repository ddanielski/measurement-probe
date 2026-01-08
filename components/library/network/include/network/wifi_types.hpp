/**
 * @file wifi_types.hpp
 * @brief WiFi type definitions and constants
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <esp_wifi_types.h>

namespace network {

/// Maximum lengths for WiFi credentials (from ESP-IDF)
inline constexpr size_t kMaxSsidLen = 32;
inline constexpr size_t kMaxPasswordLen = 64;

/// WiFi manager states (our state machine, not ESP-IDF events)
/// ESP-IDF provides wifi_event_t for low-level events; this represents
/// the manager's higher-level connection lifecycle.
enum class WifiState : uint8_t {
  Idle,         ///< Not initialized
  Disconnected, ///< Initialized but not connected
  Connecting,   ///< Connection attempt in progress
  Connected,    ///< Successfully connected with IP
  Provisioning, ///< BLE provisioning mode active
  Failed,       ///< Connection failed after max retries
};

/// WiFi credentials (stack-allocated, null-terminated)
struct WifiCredentials {
  std::array<char, kMaxSsidLen + 1> ssid{};
  std::array<char, kMaxPasswordLen + 1> password{};

  [[nodiscard]] bool is_valid() const { return ssid[0] != '\0'; }

  void clear() {
    ssid[0] = '\0';
    password[0] = '\0';
  }

  /// Set SSID from C-string
  void set_ssid(const char *value) {
    std::strncpy(ssid.data(), value, ssid.size() - 1);
    ssid.back() = '\0';
  }

  /// Set password from C-string
  void set_password(const char *value) {
    std::strncpy(password.data(), value, password.size() - 1);
    password.back() = '\0';
  }
};

/// Configuration for WiFi manager
struct WifiConfig {
  /// Maximum reconnection attempts before giving up (0 = infinite)
  uint8_t max_retries = 0;

  /// Initial backoff delay in ms
  uint32_t initial_backoff_ms = 1000;

  /// Maximum backoff delay in ms
  uint32_t max_backoff_ms = 60000;

  /// Backoff multiplier (x2 each attempt)
  static constexpr uint8_t kBackoffMultiplier = 2;
};

/// Provisioning configuration
struct ProvisioningConfig {
  /// Device name prefix (MAC suffix appended)
  const char *device_name_prefix = "PROV";

  /// Proof of Possession for security (user-provided secret)
  const char *pop = nullptr;

  /// Service UUID for BLE (nullptr uses default)
  const char *service_uuid = nullptr;

  /// Provisioning timeout in seconds (0 = no timeout)
  uint16_t timeout_sec = 300;
};

/// IPv4 address as 4-byte array
using IPv4Address = std::array<uint8_t, 4>;

/// Connection info after successful connection
struct ConnectionInfo {
  IPv4Address ip{};
  IPv4Address gateway{};
  IPv4Address netmask{};
  int8_t rssi = 0;
  uint8_t channel = 0;
};

/// WiFi manager events (published via event bus)
/// Subscribe to NETWORK_EVENTS base with these IDs
enum class NetworkEvent : int32_t {
  WifiConnected,
  WifiDisconnected,
  WifiConnectionFailed,
  ProvisioningStarted,
  ProvisioningComplete,
  ProvisioningFailed,
};

} // namespace network
