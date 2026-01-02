/**
 * @file sensor.hpp
 * @brief Sensor interface and base implementation
 */

#pragma once

#include "measurement.hpp"

#include <core/result.hpp>

#include <array>
#include <chrono>
#include <span>
#include <string_view>

namespace sensor {

/// Abstract sensor interface
class ISensor {
public:
  virtual ~ISensor() = default;

  ISensor(const ISensor &) = delete;
  ISensor &operator=(const ISensor &) = delete;
  ISensor(ISensor &&) = default;
  ISensor &operator=(ISensor &&) = default;

  /// Unique sensor name (e.g., "bme680")
  [[nodiscard]] virtual std::string_view name() const = 0;

  /// Read all measurements from sensor
  [[nodiscard]] virtual core::Result<std::span<const Measurement>> read() = 0;

  /// Put sensor into low-power sleep mode
  [[nodiscard]] virtual core::Status sleep() = 0;

  /// Wake sensor from sleep mode
  [[nodiscard]] virtual core::Status wake() = 0;

  /// Minimum interval between readings
  [[nodiscard]] virtual std::chrono::milliseconds min_interval() = 0;

  /// Number of measurements this sensor provides
  [[nodiscard]] virtual size_t measurement_count() const = 0;

protected:
  ISensor() = default;
};

/// CRTP base class for sensors with fixed measurement count
template <typename Derived, size_t N> class SensorBase : public ISensor {
public:
  [[nodiscard]] size_t measurement_count() const override { return N; }

protected:
  std::array<Measurement, N> measurements_{};

  /// Store a measurement with compile-time type enforcement
  /// Usage: store<MeasurementId::Temperature>(0, value);
  template <MeasurementId Id>
  void store(size_t index, typename MeasurementTraits<Id>::type value) {
    if (index < N) {
      measurements_[index] = make<Id>(value);
    }
  }

  /// Get measurements span
  [[nodiscard]] std::span<const Measurement> get_measurements() const {
    return std::span{measurements_};
  }
};

} // namespace sensor
