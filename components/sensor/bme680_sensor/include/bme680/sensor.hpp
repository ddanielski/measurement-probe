/**
 * @file sensor.hpp
 * @brief BME680 sensor with BSEC integration
 *
 * Implements IExternallyTimedSensor - BSEC controls sampling timing.
 * The sensor just knows how to read; Monitor handles when to read.
 *
 * Sensor ID is provided by the application via Config.
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
/// Implements IExternallyTimedSensor - BSEC dictates when to sample
class BME680Sensor final : public SensorBase<BME680Sensor, MEASUREMENT_COUNT>,
                           public IExternallyTimedSensor {
public:
  static constexpr size_t MEASUREMENT_COUNT_V = MEASUREMENT_COUNT;

  /// Configuration for the sensor
  struct Config {
    uint8_t address = driver::bme680::I2C_ADDR_SECONDARY;
    SensorIdType sensor_id = 0; ///< ID from application's SensorId enum
  };

  /// Create sensor with I2C bus, storage, and config
  BME680Sensor(driver::i2c::IMaster &bus, core::IStorage &storage,
               const Config &config);

  ~BME680Sensor() override = default;

  BME680Sensor(const BME680Sensor &) = delete;
  BME680Sensor &operator=(const BME680Sensor &) = delete;
  BME680Sensor(BME680Sensor &&) = delete;
  BME680Sensor &operator=(BME680Sensor &&) = delete;

  // ISensor interface
  [[nodiscard]] SensorIdType id() const override { return sensor_id_; }
  [[nodiscard]] std::string_view name() const override { return "bme680"; }

  [[nodiscard]] size_t measurement_count() const override {
    return MEASUREMENT_COUNT;
  }

  [[nodiscard]] std::chrono::milliseconds min_interval() const override {
    return bsec_.sample_interval();
  }

  [[nodiscard]] std::span<const Measurement> sample() override;

  // IExternallyTimedSensor interface
  [[nodiscard]] std::chrono::microseconds next_sample_delay() override;

  /// Check if sensor is ready for use
  [[nodiscard]] bool valid() const { return initialized_; }

  /// Save BSEC state to storage
  [[nodiscard]] core::Status save_state();

private:
  [[nodiscard]] core::Status init_bsec();

  driver::bme680::BME680Driver driver_;
  core::IStorage &storage_;
  BsecWrapper bsec_;
  BsecOutput last_output_{};
  SensorIdType sensor_id_;
  int64_t next_call_time_ns_ = 0;
  bool initialized_ = false;
  uint32_t sample_count_ = 0;
};

} // namespace sensor::bme680
