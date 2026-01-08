/**
 * @file sensor.cpp
 * @brief BME680 sensor implementation with BSEC
 */

#include "bme680/sensor.hpp"

#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace sensor::bme680 {

namespace {
constexpr const char *TAG = "bme680";

int64_t get_timestamp_ns() {
  return esp_timer_get_time() * 1000LL; // µs to ns
}
} // namespace

BME680Sensor::BME680Sensor(driver::i2c::IMaster &bus, core::IStorage &storage,
                           const Config &config)
    : driver_(bus, config.address), storage_(storage),
      sensor_id_(config.sensor_id) {

  // Open driver
  auto status = driver_.open();
  if (!status) {
    ESP_LOGE(TAG, "Failed to open driver: %s", esp_err_to_name(status.error()));
    return;
  }

  // Get device info
  auto info_result = driver_.ioctl(
      static_cast<uint32_t>(driver::bme680::IoctlCmd::GetDeviceInfo),
      std::any{});
  if (info_result) {
    auto info = std::any_cast<driver::bme680::DeviceInfo>(*info_result);
    ESP_LOGI(TAG, "BME6%s detected (chip ID: 0x%02X)",
             info.variant_id == 1 ? "88" : "80", info.chip_id);
  }

  // Initialize BSEC
  (void)init_bsec();
}

core::Status BME680Sensor::init_bsec() {
  if (initialized_) {
    return core::Ok();
  }

  if (!bsec_.initialized()) {
    auto status = bsec_.init();
    if (!status) {
      ESP_LOGE(TAG, "BSEC init failed");
      return status;
    }

    // Try to load saved state (non-fatal if missing)
    (void)bsec_.load_state(storage_);

    // Subscribe to outputs
    status = bsec_.subscribe_all();
    if (!status) {
      ESP_LOGE(TAG, "BSEC subscribe failed");
      return status;
    }

    ESP_LOGI(TAG, "BSEC v%s ready", bsec_.version());
  }

  initialized_ = true;
  return core::Ok();
}

std::span<const Measurement> BME680Sensor::sample() {
  if (!initialized_) {
    if (!init_bsec()) {
      return get_measurements(); // Return default/cached
    }
  }

  int64_t time_ns = get_timestamp_ns();

  // Get BSEC sensor settings
  auto settings = bsec_.get_sensor_settings(time_ns);

  // Update next call time from BSEC
  if (settings.next_call_time_ns > 0) {
    next_call_time_ns_ = settings.next_call_time_ns;
  }

  // Only process if BSEC requests data
  if (settings.process_data == 0) {
    return get_measurements(); // Return cached
  }

  // Configure BME680 based on BSEC requirements
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

  // Read raw sensor data
  std::array<uint8_t, sizeof(driver::bme680::SensorData)> buffer{};
  auto read_result = driver_.read(buffer);

  if (!read_result) {
    return get_measurements(); // Return cached on error
  }

  driver::bme680::SensorData raw{};
  std::memcpy(&raw, buffer.data(), sizeof(raw));

  // Debug: log raw sensor values
  ESP_LOGW(TAG, "Raw: T=%.2f°C P=%.2fhPa H=%.2f%% Gas=%.0fΩ valid=%d",
           raw.temperature, raw.pressure, raw.humidity, raw.gas_resistance,
           raw.gas_valid);

  // Process through BSEC (expects pressure in Pa, we have hPa)
  auto bsec_result =
      bsec_.process(time_ns, raw.temperature, raw.pressure * 100.0F,
                    raw.humidity, raw.gas_resistance, raw.gas_valid);

  auto I = [](Idx i) { return static_cast<size_t>(i); };

  if (bsec_result) {
    last_output_ = *bsec_result;

    // Debug: log BSEC outputs
    ESP_LOGW(TAG, "BSEC: IAQ=%.1f acc=%d CO2=%.0fppm VOC=%.2fppm",
             last_output_.iaq, last_output_.iaq_accuracy, last_output_.co2,
             last_output_.voc);

    // Store BSEC-processed measurements
    store<MeasurementId::Temperature>(I(Idx::Temperature),
                                      last_output_.temperature);
    store<MeasurementId::Humidity>(I(Idx::Humidity), last_output_.humidity);
    store<MeasurementId::Pressure>(I(Idx::Pressure), last_output_.pressure);
    store<MeasurementId::IAQ>(I(Idx::IAQ), last_output_.iaq);
    store<MeasurementId::IAQAccuracy>(I(Idx::IAQAccuracy),
                                      last_output_.iaq_accuracy);
    store<MeasurementId::CO2>(I(Idx::CO2), last_output_.co2);
    store<MeasurementId::VOC>(I(Idx::VOC), last_output_.voc);

    // Periodically save state (every 100 samples ≈ 5 minutes at 3s)
    sample_count_++;
    if (sample_count_ % 100 == 0) {
      (void)save_state();
    }
  } else {
    // Fall back to raw values
    store<MeasurementId::Temperature>(I(Idx::Temperature), raw.temperature);
    store<MeasurementId::Humidity>(I(Idx::Humidity), raw.humidity);
    store<MeasurementId::Pressure>(I(Idx::Pressure), raw.pressure);
    store<MeasurementId::IAQ>(I(Idx::IAQ), 0.0F);
    store<MeasurementId::IAQAccuracy>(I(Idx::IAQAccuracy), 0);
    store<MeasurementId::CO2>(I(Idx::CO2), 0.0F);
    store<MeasurementId::VOC>(I(Idx::VOC), 0.0F);
  }

  return get_measurements();
}

std::chrono::microseconds BME680Sensor::next_sample_delay() {
  int64_t now_ns = get_timestamp_ns();
  int64_t delay_ns = next_call_time_ns_ - now_ns;

  // Minimum 10ms
  delay_ns = std::max(delay_ns, 10'000'000LL);

  return std::chrono::microseconds(delay_ns / 1000LL);
}

core::Status BME680Sensor::save_state() {
  auto guard = storage_.auto_commit();
  return bsec_.save_state(storage_);
}

} // namespace sensor::bme680
