/**
 * @file sensor_ids.hpp
 * @brief Application-specific sensor ID registry
 *
 * This file defines the sensor IDs for this project.
 * Adding a new sensor only requires editing this file - no library changes needed.
 *
 * Usage:
 *   1. Add your sensor to the SensorId enum (before Count)
 *   2. Implement ISensor::id() in your sensor to return the new ID
 *   3. Register the monitor with SensorManager
 */

#pragma once

#include <cstdint>

namespace sensor {

/// Maximum number of sensors supported (for DataManager sizing)
inline constexpr size_t MAX_SENSORS = 16;

/// Maximum measurements per sensor (for DataManager sizing)
inline constexpr size_t MAX_MEASUREMENTS_PER_SENSOR = 16;

/**
 * Application-specific sensor identifiers.
 *
 * Add new sensors here. The underlying uint8_t value is used for
 * array indexing in DataManager, so keep values contiguous starting from 0.
 *
 * Count must always be the last entry - it's used to size arrays.
 */
enum class SensorId : uint8_t {
  Timestamp = 0,
  BME680 = 1,
  // Add new sensors here...
  // HDC2010 = 2,
  // ADC = 3,

  Count // Must be last - used for array sizing
};

/// Convert SensorId to array index
[[nodiscard]] constexpr size_t to_index(SensorId id) noexcept {
  return static_cast<size_t>(id);
}

/// Get the number of registered sensor types
[[nodiscard]] constexpr size_t sensor_type_count() noexcept {
  return static_cast<size_t>(SensorId::Count);
}

} // namespace sensor
