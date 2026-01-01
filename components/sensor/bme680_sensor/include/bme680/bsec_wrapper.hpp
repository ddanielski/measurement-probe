/**
 * @file bsec_wrapper.hpp
 * @brief C++ wrapper for Bosch BSEC2 library
 */

#pragma once

#include <bsec_config.h>
#include <bsec_interface.h>

#include <core/result.hpp>
#include <core/storage.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>

namespace sensor::bme680 {

/// BSEC output data
struct BsecOutput {
  float iaq;            // Indoor Air Quality (0-500)
  float static_iaq;     // Static IAQ
  float co2;            // CO2 (ppm)
  float voc;            // VOC (ppm)
  float temperature;    // Compensated temperature (°C)
  float humidity;       // Compensated humidity (%)
  float pressure;       // Pressure (Pa)
  float gas_resistance; // Raw gas resistance (Ω)
  uint8_t iaq_accuracy; // IAQ accuracy (0-3)
  bool valid;
};

/// BSEC sensor settings for BME68x
struct BsecSensorSettings {
  int64_t next_call_time_ns;
  uint32_t process_data;
  uint16_t heater_temperature;
  uint16_t heater_duration;
  uint8_t run_gas;
  uint8_t temperature_oversampling;
  uint8_t pressure_oversampling;
  uint8_t humidity_oversampling;
};

/// Callback type for reading BME68x sensor
using SensorReadCallback = std::function<bool(
    const BsecSensorSettings &settings, float &temperature, float &pressure,
    float &humidity, float &gas_resistance, bool &gas_valid)>;

/// BSEC2 wrapper class
class BsecWrapper {
public:
  BsecWrapper() = default;
  ~BsecWrapper() = default;

  BsecWrapper(const BsecWrapper &) = delete;
  BsecWrapper &operator=(const BsecWrapper &) = delete;
  BsecWrapper(BsecWrapper &&) = delete;
  BsecWrapper &operator=(BsecWrapper &&) = delete;

  /// Initialize BSEC library
  [[nodiscard]] core::Status init();

  /// Subscribe to all standard outputs
  [[nodiscard]] core::Status subscribe_all();

  /// Get sensor settings for next measurement
  [[nodiscard]] BsecSensorSettings get_sensor_settings(int64_t time_ns) const;

  /// Process sensor data through BSEC
  [[nodiscard]] core::Result<BsecOutput>
  process(int64_t time_ns, float temperature, float pressure, float humidity,
          float gas_resistance, bool gas_valid) const;

  /// Get BSEC version string
  [[nodiscard]] const char *version() const;

  /// Save BSEC state to storage
  [[nodiscard]] core::Status save_state(core::IStorage &storage);

  /// Load BSEC state from storage
  [[nodiscard]] core::Status load_state(core::IStorage &storage);

  /// Check if BSEC is initialized
  [[nodiscard]] bool initialized() const { return initialized_; }

  /// Get the configured sample interval
  [[nodiscard]] std::chrono::milliseconds sample_interval() const {
    return sample_interval_;
  }

private:
  static constexpr const char *STATE_KEY = "bsec_state";
  static constexpr size_t STATE_SIZE = BSEC_MAX_STATE_BLOB_SIZE;
  static constexpr size_t VERSION_STR_SIZE = 32; // "X.X.X.X" format

  bool initialized_ = false;
  std::array<char, VERSION_STR_SIZE> version_str_{};
  std::chrono::milliseconds sample_interval_{BSEC_CONFIGURED_INTERVAL_MS};

  // Pre-allocated work buffer (too large for stack)
  std::array<uint8_t, BSEC_MAX_WORKBUFFER_SIZE> work_buffer_{};
};

} // namespace sensor::bme680
