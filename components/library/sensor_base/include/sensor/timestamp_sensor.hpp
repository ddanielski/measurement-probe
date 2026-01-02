/**
 * @file timestamp_sensor.hpp
 * @brief Virtual sensor that provides system timestamp
 *
 * Register with SensorManager to include timestamp in read_all() results.
 */

#pragma once

#include "sensor.hpp"

#include <esp_timer.h>

namespace sensor {

/// Virtual sensor that returns current timestamp as a measurement
class TimestampSensor final : public SensorBase<TimestampSensor, 1> {
public:
  TimestampSensor() = default;
  ~TimestampSensor() override = default;

  TimestampSensor(const TimestampSensor &) = delete;
  TimestampSensor &operator=(const TimestampSensor &) = delete;
  TimestampSensor(TimestampSensor &&) = delete;
  TimestampSensor &operator=(TimestampSensor &&) = delete;

  [[nodiscard]] std::string_view name() const override { return "timestamp"; }

  [[nodiscard]] core::Result<std::span<const Measurement>> read() override {
    // Milliseconds since boot
    auto now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    store<MeasurementId::Timestamp>(0, now_ms);
    return get_measurements();
  }

  [[nodiscard]] core::Status sleep() override { return core::Ok(); }
  [[nodiscard]] core::Status wake() override { return core::Ok(); }

  [[nodiscard]] std::chrono::milliseconds min_interval() override {
    return std::chrono::milliseconds(0); // No minimum
  }
};

} // namespace sensor
