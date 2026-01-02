/**
 * @file app.cpp
 * @brief Measurement probe application implementation
 */

#include <application/app.hpp>
#include <bme680/sensor.hpp>
#include <sensor/timestamp_sensor.hpp>

#include "app_config.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cinttypes>
#include <type_traits>

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

  init_sensors();
  read_sensors();

  // TODO: Re-enable deep sleep mode
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

void MeasurementProbe::init_sensors() {
  // Register timestamp sensor first (provides timing for batches)
  sensors_.register_sensor(std::make_unique<sensor::TimestampSensor>());

  // Create and register BME680 sensor with BSEC
  sensor::bme680::BME680Sensor::Config bme_config{
      .address = app::config::BME680_ADDRESS,
  };

  // Use dedicated BSEC namespace for sensor state persistence
  auto bme680 = std::make_unique<sensor::bme680::BME680Sensor>(
      board_.i2c(), storage(core::NamespaceId::Bsec), bme_config);

  if (!bme680->valid()) {
    ESP_LOGE(TAG, "BME680 sensor not responding");
    return;
  }

  sensors_.register_sensor(std::move(bme680));

  ESP_LOGI(TAG, "Registered %zu sensor(s)", sensors_.sensor_count());
}

void MeasurementProbe::read_sensors() {
  auto result = sensors_.read_all();
  if (!result) {
    ESP_LOGE(TAG, "Sensor read failed: %s", esp_err_to_name(result.error()));
    return;
  }

  ESP_LOGI(TAG, "--- Sensor Readings ---");
  for (const auto &m : *result) {
    m.visit([&m](auto &&v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, bool>) {
        ESP_LOGI(TAG, "  %s: %s", m.name(), v ? "true" : "false");
      } else if constexpr (std::is_same_v<T, uint64_t>) {
        ESP_LOGI(TAG, "  %s: %llu %s", m.name(), v, m.unit());
      } else if constexpr (std::is_same_v<T, int64_t>) {
        ESP_LOGI(TAG, "  %s: %lld %s", m.name(), v, m.unit());
      } else if constexpr (std::is_same_v<T, uint32_t>) {
        ESP_LOGI(TAG, "  %s: %lu %s", m.name(), static_cast<unsigned long>(v),
                 m.unit());
      } else if constexpr (std::is_same_v<T, int32_t>) {
        ESP_LOGI(TAG, "  %s: %ld %s", m.name(), static_cast<long>(v), m.unit());
      } else if constexpr (std::is_same_v<T, uint8_t>) {
        ESP_LOGI(TAG, "  %s: %u %s", m.name(), static_cast<unsigned>(v),
                 m.unit());
      } else if constexpr (std::is_floating_point_v<T>) {
        ESP_LOGI(TAG, "  %s: %.2f %s", m.name(), static_cast<double>(v),
                 m.unit());
      }
    });
  }
}

void MeasurementProbe::run_continuous_mode() {
  auto interval_ms = sensors_.sample_interval().count();
  ESP_LOGI(TAG, "Deep sleep disabled - reading sensors every %lldms",
           static_cast<long long>(interval_ms));

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(interval_ms));
    read_sensors();
  }
}

} // namespace application
