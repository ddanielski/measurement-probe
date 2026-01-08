/**
 * @file events.hpp
 * @brief Sensor event definitions
 */

#pragma once

#include "sensor.hpp"

#include <core/event_loop.hpp>

#include <cstdint>

/// Sensor events base
CORE_EVENT_DECLARE_BASE(SENSOR_EVENTS);

namespace sensor {

/// Sensor events (use with SENSOR_EVENTS base)
/// Note: ESP-IDF event API uses int32_t internally, but we use uint8_t for
/// storage
enum class SensorEvent : uint8_t {
  DataReady, ///< A sensor has new data available
};

/// Payload for SensorEvent::DataReady
struct SensorDataEvent {
  SensorIdType sensor_id; ///< Which sensor produced data
  size_t count;           ///< Number of measurements
};

} // namespace sensor
