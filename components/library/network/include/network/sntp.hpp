/**
 * @file sntp.hpp
 * @brief Simple NTP time synchronization with event-driven auto-init
 *
 * SNTP automatically initializes when WiFi connects. No manual registration
 * needed - just call sntp() to access the singleton.
 *
 * Optionally call sntp().configure() before WiFi connects to customize
 * settings.
 */

#pragma once

#include <core/event_loop.hpp>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <cstdint>

namespace network {

/// SNTP configuration
struct SntpConfig {
  /// Primary NTP server (default: pool.ntp.org)
  const char *server = "pool.ntp.org";

  /// Sync interval in hours (default: 1 hour)
  uint8_t sync_interval_hours = 1;

  /// Timezone string (POSIX format, default: UTC)
  /// Examples: "UTC", "EST5EDT", "CET-1CEST,M3.5.0,M10.5.0/3"
  const char *timezone = "UTC";
};

/// SNTP time synchronization service (singleton)
/// Auto-registers for WiFi events on first access
class Sntp {
public:
  /// Get singleton instance (auto-registers on first call)
  static Sntp &instance();

  Sntp(const Sntp &) = delete;
  Sntp &operator=(const Sntp &) = delete;

  /// Configure SNTP settings (call before WiFi connects)
  void configure(const SntpConfig &config);

  /// Check if time has been synchronized
  [[nodiscard]] bool is_synced() const;

  /// Wait for time synchronization (blocking)
  /// @param timeout_ms Maximum time to wait (0 = wait forever)
  /// @return true if synced, false if timeout
  [[nodiscard]] bool wait_for_sync(uint32_t timeout_ms = 0);

  /// Get Unix timestamp in seconds (0 if not synced)
  [[nodiscard]] uint64_t time() const;

  /// Get Unix timestamp in milliseconds (0 if not synced)
  [[nodiscard]] uint64_t time_ms() const;

private:
  Sntp();
  ~Sntp() = default;

  void register_events();
  void do_init();

  static void on_time_sync(struct timeval *tv);
  static void on_network_event(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data);

  /// Event group bits
  static constexpr EventBits_t kBitRegistered = BIT0;
  static constexpr EventBits_t kBitInitialized = BIT1;
  static constexpr EventBits_t kBitSynced = BIT2;

  EventGroupHandle_t event_group_ = nullptr;
  SntpConfig config_{};
  core::EventSubscription wifi_sub_;
};

/// Get SNTP singleton
inline Sntp &sntp() { return Sntp::instance(); }

} // namespace network
