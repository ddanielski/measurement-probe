/**
 * @file data_manager.cpp
 * @brief DataManager implementation (vtable anchor)
 *
 * Note: SENSOR_EVENTS base is defined in sensor.cpp to avoid duplicate symbols.
 */

#include <sensor/data_manager.hpp>

namespace sensor {
// Ensures IDataHandler vtable is emitted in this translation unit
} // namespace sensor
