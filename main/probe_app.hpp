/**
 * @file probe_app.hpp
 * @brief Measurement probe application
 */

#pragma once

#include "app_config.hpp"

#include <bme680/sensor.hpp>
#include <core/core.hpp>
#include <i2c/i2c.hpp>
#include <power/sleep.hpp>
#include <sensor/sensor_all.hpp>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>

class MeasurementProbe final : public core::Application {
public:
  MeasurementProbe()
      : sleep_(std::chrono::seconds(app::config::SLEEP_INTERVAL_SEC)) {}

protected:
  void run() override {
    auto wake = power::get_wake_reason();
    ESP_LOGI(TAG, "v%s | Wake: %s", app::config::FIRMWARE_VERSION,
             power::to_string(wake));

    track_boot_count();
    init_sensors();
    read_sensors();

    // TODO: Re-enable deep sleep
    // ESP_LOGI(TAG, "Sleeping %llds",
    //          static_cast<long long>(sleep_.interval().count()));
    // vTaskDelay(pdMS_TO_TICKS(1000));
    // sleep_.enter();

    auto interval_ms = sensors_.sample_interval().count();
    ESP_LOGI(TAG, "Deep sleep disabled - reading sensors every %lldms",
             static_cast<long long>(interval_ms));
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(interval_ms));
      read_sensors();
    }
  }

private:
  static constexpr const char *TAG = "probe";

  void track_boot_count() {
    auto guard = storage().auto_commit();
    uint32_t boots = storage().get<uint32_t>("boots").value_or(0) + 1;
    if (storage().set<uint32_t>("boots", boots).ok()) {
      ESP_LOGI(TAG, "Boot #%" PRIu32, boots);
    }
  }

  void init_sensors() {
    // Initialize I2C bus
    driver::i2c::Config i2c_config{
        .sda_pin = app::config::I2C_SDA_PIN,
        .scl_pin = app::config::I2C_SCL_PIN,
    };

    i2c_bus_ = std::make_unique<driver::i2c::Master>(i2c_config);
    if (!i2c_bus_->valid()) {
      ESP_LOGE(TAG, "Failed to initialize I2C bus");
      return;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)",
             app::config::I2C_SDA_PIN, app::config::I2C_SCL_PIN);

    // Create and register BME680 sensor with BSEC
    sensor::bme680::BME680Sensor::Config bme_config{
        .address = app::config::BME680_ADDRESS,
    };

    auto bme680 = std::make_unique<sensor::bme680::BME680Sensor>(
        *i2c_bus_, storage(), bme_config);

    if (!bme680->valid()) {
      ESP_LOGE(TAG, "BME680 sensor not responding");
      return;
    }

    bme680_ = bme680.get(); // Keep reference for IAQ accuracy

    sensors_.register_sensor(std::move(bme680));
    ESP_LOGI(TAG, "Registered %zu sensor(s)", sensors_.sensor_count());
  }

  void read_sensors() {
    auto result = sensors_.read_all();
    if (!result.ok()) {
      ESP_LOGE(TAG, "Sensor read failed: %s", esp_err_to_name(result.error()));
      return;
    }

    ESP_LOGI(TAG, "--- Sensor Readings (IAQ accuracy: %d/3) ---",
             bme680_ != nullptr ? bme680_->iaq_accuracy() : 0);
    for (const auto &m : result.value()) {
      ESP_LOGI(TAG, "  %s: %.2f %s", m.name(), m.value, m.unit());
    }
  }

  power::DeepSleep sleep_;
  std::unique_ptr<driver::i2c::Master> i2c_bus_;
  sensor::SensorManager sensors_;
  sensor::bme680::BME680Sensor *bme680_ = nullptr;
};
