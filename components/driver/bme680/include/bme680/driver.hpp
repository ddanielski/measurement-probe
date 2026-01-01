/**
 * @file driver.hpp
 * @brief BME680 driver wrapping Bosch BME68x API (VFS-style)
 */

#pragma once

#include <bme68x.h>

#include <driver/driver.hpp>
#include <i2c/i2c.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace driver::bme680 {

/// BME680 I2C addresses
inline constexpr uint8_t I2C_ADDR_PRIMARY = 0x76;   // SDO to GND
inline constexpr uint8_t I2C_ADDR_SECONDARY = 0x77; // SDO to VCC

/// Sensor data from BME680
struct SensorData {
  float temperature;    // °C
  float pressure;       // hPa
  float humidity;       // %RH
  float gas_resistance; // Ohms
  bool gas_valid;
  bool heater_stable;
};

/// Configuration for BME680
struct Config {
  uint8_t temp_os = BME68X_OS_4X;
  uint8_t pres_os = BME68X_OS_4X;
  uint8_t hum_os = BME68X_OS_4X;
  uint8_t filter = BME68X_FILTER_SIZE_3;
  uint8_t odr = BME68X_ODR_NONE;
  uint16_t heater_temp = 320; // °C
  uint16_t heater_dur = 150;  // ms
  bool enable_gas = true;
};

/// Device info structure
struct DeviceInfo {
  uint8_t chip_id;
  uint32_t variant_id; // 0 = BME680, 1 = BME688
};

/// ioctl commands
// NOLINTNEXTLINE(performance-enum-size)
enum class IoctlCmd : uint32_t {
  Configure,              // arg: const Config*
  TriggerMeasurement,     // arg: nullptr
  GetMeasurementDuration, // arg: std::chrono::microseconds*
  GetDeviceInfo,          // arg: DeviceInfo*
  ReadData,               // arg: SensorData*
};

/// Low-level BME680 driver (VFS-style)
class BME680Driver : public IDriver {
public:
  BME680Driver(i2c::IMaster &bus, uint8_t address = I2C_ADDR_SECONDARY);
  ~BME680Driver() override;

  BME680Driver(const BME680Driver &) = delete;
  BME680Driver &operator=(const BME680Driver &) = delete;
  BME680Driver(BME680Driver &&) = delete;
  BME680Driver &operator=(BME680Driver &&) = delete;

  // VFS-style interface (self-managing: returns ESP_ERR_INVALID_STATE if not
  // open)
  [[nodiscard]] core::Status open() override;
  [[nodiscard]] core::Status close() override;
  [[nodiscard]] core::Result<size_t> read(std::span<uint8_t> buffer) override;
  [[nodiscard]] core::Result<size_t>
  write(std::span<const uint8_t> data) override;
  [[nodiscard]] core::Result<std::any> ioctl(uint32_t cmd,
                                             std::any arg) override;

private:
  // Internal implementations called via ioctl
  [[nodiscard]] core::Status configure_impl(const Config &config);
  [[nodiscard]] core::Status trigger_measurement_impl();
  [[nodiscard]] std::chrono::microseconds measurement_duration_impl();
  [[nodiscard]] core::Result<SensorData> read_data_impl();

  // Bosch API callbacks
  static BME68X_INTF_RET_TYPE i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                       uint32_t length, void *intf_ptr);
  static BME68X_INTF_RET_TYPE i2c_write(uint8_t reg_addr,
                                        const uint8_t *reg_data,
                                        uint32_t length, void *intf_ptr);
  static void delay_us(uint32_t period, void *intf_ptr);

  std::unique_ptr<i2c::IDevice> device_;
  bme68x_dev dev_{};
  bme68x_conf conf_{};
  bme68x_heatr_conf heatr_conf_{};
  bool is_open_ = false;
};

} // namespace driver::bme680
