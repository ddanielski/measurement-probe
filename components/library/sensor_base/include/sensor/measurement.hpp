/**
 * @file measurement.hpp
 * @brief Measurement types and metadata for sensors
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sensor {

/// Measurement type identifiers
enum class MeasurementId : uint8_t {
  // Environmental
  Temperature,
  Humidity,
  Pressure,

  // Air quality
  IAQ,         // Indoor Air Quality index
  IAQAccuracy, // IAQ calibration accuracy (0-3)
  CO2,         // Estimated CO2 in ppm
  VOC,         // Estimated VOC in ppm

  // Light
  Illuminance,
  UVIndex,

  // Motion
  AccelX,
  AccelY,
  AccelZ,
  GyroX,
  GyroY,
  GyroZ,

  // Other
  Distance,
  Voltage,
  Current,

  Count // Must be last
};

/// Compile-time metadata for measurements (stored in flash)
struct MeasurementMeta {
  const char *name;
  const char *unit;
};

/// Measurement metadata lookup table
inline constexpr std::array<MeasurementMeta,
                            static_cast<size_t>(MeasurementId::Count)>
    MEASUREMENT_META = {{
        // Environmental
        {.name = "temperature", .unit = "°C"},
        {.name = "humidity", .unit = "%"},
        {.name = "pressure", .unit = "hPa"},

        // Air quality
        {.name = "iaq", .unit = ""},
        {.name = "iaq_accuracy", .unit = "/3"},
        {.name = "co2", .unit = "ppm"},
        {.name = "voc", .unit = "ppm"},

        // Light
        {.name = "illuminance", .unit = "lux"},
        {.name = "uv_index", .unit = ""},

        // Motion
        {.name = "accel_x", .unit = "g"},
        {.name = "accel_y", .unit = "g"},
        {.name = "accel_z", .unit = "g"},
        {.name = "gyro_x", .unit = "°/s"},
        {.name = "gyro_y", .unit = "°/s"},
        {.name = "gyro_z", .unit = "°/s"},

        // Other
        {.name = "distance", .unit = "m"},
        {.name = "voltage", .unit = "V"},
        {.name = "current", .unit = "A"},
    }};

/// Runtime measurement value (minimal footprint)
struct Measurement {
  MeasurementId id;
  float value;

  /// Get metadata for this measurement
  [[nodiscard]] const MeasurementMeta &meta() const {
    return MEASUREMENT_META.at(static_cast<size_t>(id));
  }

  [[nodiscard]] const char *name() const { return meta().name; }
  [[nodiscard]] const char *unit() const { return meta().unit; }
};

} // namespace sensor
