/**
 * @file sensor.hpp
 * @brief BME680 high-level sensor with BSEC integration
 */

#pragma once

#include "bsec_wrapper.hpp"

#include <bme680/driver.hpp>
#include <core/storage.hpp>
#include <sensor/sensor.hpp>

namespace sensor::bme680 {

/// Measurement indices for BME680 sensor
enum class Idx : size_t {
  Temperature,
  Humidity,
  Pressure,
  IAQ,
  IAQAccuracy,
  CO2,
  VOC,
  Count
};

/// Number of measurements provided by BME680 with BSEC
inline constexpr size_t MEASUREMENT_COUNT = static_cast<size_t>(Idx::Count);

/// BME680 sensor with BSEC for IAQ/CO2/VOC
class BME680Sensor final : public SensorBase<BME680Sensor, MEASUREMENT_COUNT> {
public:
  /// Configuration for the sensor
  struct Config {
    uint8_t address = driver::bme680::I2C_ADDR_SECONDARY;
  };

  /// Create sensor with I2C bus and storage for BSEC state
  BME680Sensor(driver::i2c::IMaster &bus, core::IStorage &storage);

  /// Create sensor with config
  BME680Sensor(driver::i2c::IMaster &bus, core::IStorage &storage,
               const Config &config);

  ~BME680Sensor() override = default;

  BME680Sensor(const BME680Sensor &) = delete;
  BME680Sensor &operator=(const BME680Sensor &) = delete;
  BME680Sensor(BME680Sensor &&) = delete;
  BME680Sensor &operator=(BME680Sensor &&) = delete;

  // ISensor interface
  [[nodiscard]] std::string_view name() const override { return "bme680"; }
  [[nodiscard]] core::Result<std::span<const Measurement>> read() override;
  [[nodiscard]] core::Status sleep() override;
  [[nodiscard]] core::Status wake() override;
  [[nodiscard]] std::chrono::milliseconds min_interval() override;

  /// Check if sensor is ready for use
  [[nodiscard]] bool valid() const;

  /// Save BSEC state
  [[nodiscard]] core::Status save_state();

private:
  [[nodiscard]] core::Status init_bsec();

  driver::bme680::BME680Driver driver_;
  core::IStorage &storage_;
  BsecWrapper bsec_;
  BsecOutput last_output_{};
  bool initialized_ = false;
  uint32_t read_count_ = 0;
};

} // namespace sensor::bme680
