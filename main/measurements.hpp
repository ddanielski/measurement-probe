/**
 * @file measurements.hpp
 * @brief Measurement Configuration - Define what sensors to read
 *
 * This file configures which sensors are available and what measurements
 * to take. The application layer is aware of the specific hardware.
 *
 * @note Add new sensors here as they are integrated
 */

#pragma once

#include <array>
#include <cstdint>

namespace app::measurements {

// =============================================================================
// I²C Bus Configuration
// =============================================================================

/// I²C bus used for sensors
inline constexpr uint8_t I2C_PORT = 0;

/// I²C SDA pin (ESP32-C3 Super Mini)
inline constexpr uint8_t I2C_SDA_PIN = 8;

/// I²C SCL pin (ESP32-C3 Super Mini)
inline constexpr uint8_t I2C_SCL_PIN = 9;

/// I²C bus speed in Hz
inline constexpr uint32_t I2C_FREQ_HZ = 400'000;  // 400 kHz (Fast mode)

// =============================================================================
// Sensor Addresses
// =============================================================================

/// BME680 I²C address (SDO to GND = 0x76, SDO to VCC = 0x77)
inline constexpr uint8_t BME680_I2C_ADDR = 0x76;

// =============================================================================
// Measurement Types
// =============================================================================

/**
 * @brief Enum of all possible measurement types
 * @note Keep in sync with protobuf SensorType enum
 */
enum class MeasurementType : uint8_t {
    TEMPERATURE = 0,    ///< Temperature in °C
    HUMIDITY = 1,       ///< Relative humidity in %
    PRESSURE = 2,       ///< Barometric pressure in hPa
    GAS_RESISTANCE = 3, ///< Gas resistance in Ω (raw)
    IAQ = 4,            ///< Indoor Air Quality index (BSEC)
    BATTERY_VOLTAGE = 5 ///< Battery voltage in mV
};

// =============================================================================
// Enabled Measurements
// =============================================================================

/**
 * @brief List of measurements to take on each wake cycle
 *
 * The application will iterate through this list and read each measurement
 * from the appropriate sensor driver.
 */
inline constexpr std::array ENABLED_MEASUREMENTS = {
    MeasurementType::TEMPERATURE,
    MeasurementType::HUMIDITY,
    MeasurementType::PRESSURE,
    MeasurementType::IAQ,           // Requires BSEC
    MeasurementType::BATTERY_VOLTAGE
};

// =============================================================================
// Sensor-specific Configuration
// =============================================================================

namespace bme680 {

/// Oversampling for temperature (1x, 2x, 4x, 8x, 16x)
inline constexpr uint8_t TEMP_OVERSAMPLING = 2;  // 2x

/// Oversampling for pressure
inline constexpr uint8_t PRESSURE_OVERSAMPLING = 4;  // 4x

/// Oversampling for humidity
inline constexpr uint8_t HUMIDITY_OVERSAMPLING = 1;  // 1x

/// IIR filter coefficient (0=off, 1, 3, 7, 15, 31, 63, 127)
inline constexpr uint8_t IIR_FILTER = 3;

/// Gas heater temperature in °C
inline constexpr uint16_t GAS_HEATER_TEMP_C = 320;

/// Gas heater duration in ms
inline constexpr uint16_t GAS_HEATER_DURATION_MS = 150;

}  // namespace bme680

// =============================================================================
// Battery Monitoring
// =============================================================================

namespace battery {

/// ADC pin for battery voltage measurement (via voltage divider)
inline constexpr uint8_t ADC_PIN = 2;  // GPIO2

/// Voltage divider ratio (e.g., 2.0 for 1:1 divider)
/// R1=100k to battery, R2=100k to GND -> ratio = 2.0
inline constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;

/// ADC reference voltage in mV
inline constexpr uint32_t ADC_VREF_MV = 1100;

}  // namespace battery

}  // namespace app::measurements

