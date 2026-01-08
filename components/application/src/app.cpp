/**
 * @file app.cpp
 * @brief Measurement probe application implementation
 */

#include <application/app.hpp>
#include <sensor/log.hpp>

#include "app_config.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <span>

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

  run_continuous_mode();
}

void MeasurementProbe::log_boot_info() {
  auto wake = power::get_wake_reason();
  ESP_LOGI(TAG, "v%s | Wake: %s", app::config::FIRMWARE_VERSION,
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

  // Set up state change callback
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
    break;
  }
  case network::WifiState::Disconnected:
    ESP_LOGW(TAG, "WiFi disconnected");
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
}

void MeasurementProbe::run_continuous_mode() {
  ESP_LOGI(TAG, "Running - monitors sample independently, logging every 10s");

  // Monitors run on their own timers
  // Just keep the main task alive
  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}

} // namespace application
