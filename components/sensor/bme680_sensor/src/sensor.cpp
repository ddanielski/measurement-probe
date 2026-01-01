/**
 * @file sensor.cpp
 * @brief BME680 sensor implementation with BSEC
 */

#include "bme680/sensor.hpp"

#include <esp_log.h>
#include <esp_timer.h>

#include <array>
#include <cstring>

namespace sensor::bme680 {

namespace {
constexpr const char *TAG = "bme680";

int64_t get_timestamp_ns() {
  return esp_timer_get_time() * 1000LL; // µs to ns
}
} // namespace

BME680Sensor::BME680Sensor(driver::i2c::IMaster &bus, core::IStorage &storage)
    : BME680Sensor(bus, storage, Config{}) {}

BME680Sensor::BME680Sensor(driver::i2c::IMaster &bus, core::IStorage &storage,
                           const Config &config)
    : driver_(bus, config.address), storage_(storage) {

  // Open driver
  auto status = driver_.open();
  if (!status.ok()) {
    ESP_LOGE(TAG, "Failed to open driver: %s", esp_err_to_name(status.error()));
    return;
  }

  // Get device info
  auto info_result = driver_.ioctl(
      static_cast<uint32_t>(driver::bme680::IoctlCmd::GetDeviceInfo),
      std::any{});
  if (info_result.ok()) {
    auto info = std::any_cast<driver::bme680::DeviceInfo>(info_result.value());
    ESP_LOGI(TAG, "BME6%s detected (chip ID: 0x%02X)",
             info.variant_id == 1 ? "88" : "80", info.chip_id);
  }

  // Try to initialize BSEC (non-fatal if it fails, will retry on read)
  (void)init_bsec();
}

core::Status BME680Sensor::init_bsec() {
  if (initialized_) {
    return ESP_OK;
  }

  if (!bsec_.initialized()) {
    auto status = bsec_.init();
    if (!status.ok()) {
      ESP_LOGE(TAG, "BSEC init failed");
      return status;
    }

    // Try to load saved state (non-fatal if missing)
    (void)bsec_.load_state(storage_);

    // Subscribe to outputs
    status = bsec_.subscribe_all();
    if (!status.ok()) {
      ESP_LOGE(TAG, "BSEC subscribe failed");
      return status;
    }

    ESP_LOGI(TAG, "BSEC v%s ready", bsec_.version());
  }

  initialized_ = true;
  return ESP_OK;
}

core::Result<std::span<const Measurement>> BME680Sensor::read() {
  if (!initialized_) {
    auto status = init_bsec();
    if (!status.ok()) {
      return status.error();
    }
  }

  int64_t time_ns = get_timestamp_ns();

  // Get BSEC sensor settings
  auto settings = bsec_.get_sensor_settings(time_ns);

  // Configure BME680 based on BSEC requirements
  if (settings.process_data != 0) {
    driver::bme680::Config config{};
    config.temp_os = settings.temperature_oversampling;
    config.pres_os = settings.pressure_oversampling;
    config.hum_os = settings.humidity_oversampling;
    config.heater_temp = settings.heater_temperature;
    config.heater_dur = settings.heater_duration;
    config.enable_gas = settings.run_gas != 0;

    const driver::bme680::Config *config_ptr = &config;
    (void)driver_.ioctl(
        static_cast<uint32_t>(driver::bme680::IoctlCmd::Configure), config_ptr);
  }

  // Read raw sensor data
  std::array<uint8_t, sizeof(driver::bme680::SensorData)> buffer{};
  auto read_result = driver_.read(buffer);
  if (!read_result.ok()) {
    return read_result.error();
  }

  driver::bme680::SensorData raw{};
  std::memcpy(&raw, buffer.data(), sizeof(raw));

  // Process through BSEC
  // Note: BSEC expects pressure in Pa, we have hPa
  auto bsec_result =
      bsec_.process(time_ns, raw.temperature, raw.pressure * 100.0F,
                    raw.humidity, raw.gas_resistance, raw.gas_valid);

  auto I = [](Idx i) { return static_cast<size_t>(i); };

  if (!bsec_result.ok()) {
    // Fall back to raw values
    store(I(Idx::Temperature), MeasurementId::Temperature, raw.temperature);
    store(I(Idx::Humidity), MeasurementId::Humidity, raw.humidity);
    store(I(Idx::Pressure), MeasurementId::Pressure, raw.pressure);
    store(I(Idx::IAQ), MeasurementId::IAQ, 0.0F);
    store(I(Idx::IAQAccuracy), MeasurementId::IAQAccuracy, 0.0F);
    store(I(Idx::CO2), MeasurementId::CO2, 0.0F);
    store(I(Idx::VOC), MeasurementId::VOC, 0.0F);
    return get_measurements();
  }

  last_output_ = bsec_result.value();

  // Store BSEC-processed measurements
  store(I(Idx::Temperature), MeasurementId::Temperature,
        last_output_.temperature);
  store(I(Idx::Humidity), MeasurementId::Humidity, last_output_.humidity);
  store(I(Idx::Pressure), MeasurementId::Pressure, last_output_.pressure);
  store(I(Idx::IAQ), MeasurementId::IAQ, last_output_.iaq);
  store(I(Idx::IAQAccuracy), MeasurementId::IAQAccuracy,
        static_cast<float>(last_output_.iaq_accuracy));
  store(I(Idx::CO2), MeasurementId::CO2, last_output_.co2);
  store(I(Idx::VOC), MeasurementId::VOC, last_output_.voc);

  // Periodically save state (every 100 reads ≈ 5 minutes at 3s interval)
  read_count_++;
  if (read_count_ % 100 == 0) {
    (void)save_state();
  }

  return get_measurements();
}

core::Status BME680Sensor::sleep() {
  // Nothing to do if not initialized
  if (!initialized_) {
    return ESP_OK;
  }
  (void)save_state(); // Save state before sleep
  return driver_.close();
}

core::Status BME680Sensor::wake() {
  // Self-healing: try to initialize if not ready
  if (!initialized_) {
    return init_bsec();
  }
  return driver_.open();
}

std::chrono::milliseconds BME680Sensor::min_interval() {
  return bsec_.sample_interval();
}

bool BME680Sensor::valid() const { return initialized_; }

core::Status BME680Sensor::save_state() {
  auto guard = storage_.auto_commit();
  return bsec_.save_state(storage_);
}

} // namespace sensor::bme680
