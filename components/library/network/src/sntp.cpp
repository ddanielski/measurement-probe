/**
 * @file sntp.cpp
 * @brief SNTP time synchronization implementation
 */

#include "network/sntp.hpp"
#include "network/wifi_manager.hpp" // For NETWORK_EVENTS

#include <esp_log.h>
#include <esp_sntp.h>

#include <ctime>
#include <sys/time.h>

namespace network {

namespace {
constexpr const char *TAG = "sntp";
constexpr int kMinValidYear = 2026;
} // namespace

Sntp &Sntp::instance() {
  static Sntp instance;
  return instance;
}

Sntp::Sntp() {
  event_group_ = xEventGroupCreate();
  register_events();
}

void Sntp::configure(const SntpConfig &config) {
  // Only allow configuration before initialization
  if ((xEventGroupGetBits(event_group_) & kBitInitialized) == 0) {
    config_ = config;
  }
}

void Sntp::register_events() {
  if ((xEventGroupGetBits(event_group_) & kBitRegistered) != 0) {
    return;
  }

  wifi_sub_ = core::events().subscribe(
      NETWORK_EVENTS, NetworkEvent::WifiConnected, on_network_event, this);

  xEventGroupSetBits(event_group_, kBitRegistered);
  ESP_LOGI(TAG, "Registered for WiFi events");
}

void Sntp::do_init() {
  if ((xEventGroupGetBits(event_group_) & kBitInitialized) != 0) {
    return;
  }

  // Set timezone
  setenv("TZ", config_.timezone, 1);
  tzset();

  ESP_LOGI(TAG, "Initializing SNTP with server: %s", config_.server);

  // Configure SNTP
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, config_.server);
  esp_sntp_set_time_sync_notification_cb(on_time_sync);

  // Set sync interval (convert hours to milliseconds)
  esp_sntp_set_sync_interval(config_.sync_interval_hours * 3600 * 1000);

  // Start SNTP
  esp_sntp_init();

  xEventGroupSetBits(event_group_, kBitInitialized);
  ESP_LOGI(TAG, "SNTP initialized, waiting for sync...");
}

void Sntp::on_time_sync(struct timeval * /*tv*/) {
  ESP_LOGI(TAG, "Time synchronized");
  xEventGroupSetBits(instance().event_group_, kBitSynced);
}

void Sntp::on_network_event(void *arg, esp_event_base_t /*base*/,
                            int32_t event_id, void * /*event_data*/) {
  if (event_id == static_cast<int32_t>(NetworkEvent::WifiConnected)) {
    static_cast<Sntp *>(arg)->do_init();
  }
}

bool Sntp::is_synced() const {
  // Check event group flag first (fast path)
  if ((xEventGroupGetBits(event_group_) & kBitSynced) == 0) {
    return false;
  }

  // Validate year as safety check
  time_t now = 0;
  tm timeinfo{};
  ::time(&now);
  localtime_r(&now, &timeinfo);

  return timeinfo.tm_year >= (kMinValidYear - 1900);
}

bool Sntp::wait_for_sync(uint32_t timeout_ms) {
  TickType_t ticks =
      (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

  EventBits_t bits =
      xEventGroupWaitBits(event_group_, kBitSynced, pdFALSE, pdTRUE, ticks);

  return (bits & kBitSynced) != 0;
}

uint64_t Sntp::time() const {
  if (!is_synced()) {
    return 0;
  }

  time_t now = 0;
  ::time(&now);
  return static_cast<uint64_t>(now);
}

uint64_t Sntp::time_ms() const {
  if (!is_synced()) {
    return 0;
  }

  timeval tv{};
  gettimeofday(&tv, nullptr);
  return (static_cast<uint64_t>(tv.tv_sec) * 1000) +
         (static_cast<uint64_t>(tv.tv_usec) / 1000);
}

} // namespace network
