/**
 * @file sensor.hpp
 * @brief ISensor interface - pure hardware abstraction
 *
 * Sensors implement this interface to provide hardware reading capability.
 * They do NOT manage timing or caching - that's the Monitor's job.
 *
 * Note: SensorId is defined in the application layer (sensor_ids.hpp).
 * This allows adding sensors without modifying the library.
 */

#pragma once

#include "measurement.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

namespace sensor {

/// Type used for sensor identification (actual enum defined in application)
using SensorIdType = uint8_t;

/// Interface for sensor hardware abstraction
/// Implementations know HOW to read, not WHEN
class ISensor {
public:
  virtual ~ISensor() = default;

  /// Sensor type identifier (used as key in DataManager)
  /// Returns the underlying value of application-defined SensorId
  [[nodiscard]] virtual SensorIdType id() const = 0;

  /// Human-readable sensor name (for logging)
  [[nodiscard]] virtual std::string_view name() const = 0;

  /// Number of measurements this sensor provides
  [[nodiscard]] virtual size_t measurement_count() const = 0;

  /// Minimum interval between samples (hardware limitation)
  [[nodiscard]] virtual std::chrono::milliseconds min_interval() const = 0;

  /// Sample the sensor - reads hardware, returns measurements
  /// The returned span must remain valid until next sample() call
  [[nodiscard]] virtual std::span<const Measurement> sample() = 0;
};

/// Interface for externally-timed sensors (e.g., BSEC-controlled)
/// These sensors tell the caller when to sample next
class IExternallyTimedSensor : public ISensor {
public:
  /// Get delay until next sample should occur
  [[nodiscard]] virtual std::chrono::microseconds next_sample_delay() = 0;
};

/// Helper base class for sensors with fixed measurement count
/// Provides storage and store() helper
template <typename Derived, size_t N> class SensorBase {
public:
  static constexpr size_t MEASUREMENT_COUNT = N;

  [[nodiscard]] size_t measurement_count() const { return N; }

protected:
  SensorBase() = default;
  ~SensorBase() = default;

  std::array<Measurement, N> measurements_{};

  /// Store a measurement with compile-time type enforcement
  template <MeasurementId Id>
  void store(size_t index, typename MeasurementTraits<Id>::type value) {
    if (index < N) {
      measurements_[index] = make<Id>(value);
    }
  }

  /// Get measurements span (for sample() return)
  [[nodiscard]] std::span<const Measurement> get_measurements() const {
    return std::span<const Measurement>(measurements_.data(), N);
  }
};

} // namespace sensor
