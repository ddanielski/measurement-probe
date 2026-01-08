/**
 * @file timestamp_sensor.hpp
 * @brief Virtual sensor that provides NTP-synchronized timestamp
 *
 * Returns Unix timestamp in milliseconds from NTP, or 0 if not synchronized.
 * This is a "degenerate" sensor with MIN_INTERVAL = 0 (instant read).
 */

#pragma once

#include "sensor_ids.hpp"

#include <sensor/sensor.hpp>

#include <network/sntp.hpp>

namespace application {

/// Virtual sensor that returns current NTP timestamp
class TimestampSensor final : public sensor::SensorBase<TimestampSensor, 1>,
                              public sensor::ISensor {
public:
  static constexpr size_t MEASUREMENT_COUNT = 1;

  TimestampSensor() = default;
  ~TimestampSensor() override = default;

  TimestampSensor(const TimestampSensor &) = delete;
  TimestampSensor &operator=(const TimestampSensor &) = delete;
  TimestampSensor(TimestampSensor &&) = delete;
  TimestampSensor &operator=(TimestampSensor &&) = delete;

  // ISensor interface
  [[nodiscard]] sensor::SensorIdType id() const override {
    return static_cast<sensor::SensorIdType>(sensor::SensorId::Timestamp);
  }

  [[nodiscard]] std::string_view name() const override { return "timestamp"; }

  [[nodiscard]] size_t measurement_count() const override {
    return MEASUREMENT_COUNT;
  }

  [[nodiscard]] std::chrono::milliseconds min_interval() const override {
    return std::chrono::milliseconds(0); // Instant - no hardware delay
  }

  [[nodiscard]] std::span<const sensor::Measurement> sample() override {
    auto now_ms = network::sntp().time_ms();
    store<sensor::MeasurementId::Timestamp>(0, now_ms);
    return get_measurements();
  }
};

} // namespace application
