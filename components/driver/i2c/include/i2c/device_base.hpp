/**
 * @file device_base.hpp
 * @brief Base class for I2C device drivers (sensors, etc.)
 */

#pragma once

#include "interface.hpp"
#include "types.hpp"

#include <esp_err.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace driver::i2c {

/**
 * @brief Base class for I2C peripheral drivers
 *
 * Sensor and peripheral drivers should inherit from this class.
 * It provides common I2C operations and manages the device handle.
 *
 * Example usage:
 * @code
 * class BME280 : public DeviceBase {
 * public:
 *   static constexpr uint16_t DEFAULT_ADDRESS = 0x76;
 *
 *   BME280(IMaster& bus, uint16_t addr = DEFAULT_ADDRESS)
 *       : DeviceBase(bus, addr) {}
 *
 *   esp_err_t init() {
 *     uint8_t chip_id = 0;
 *     if (auto err = read_register(REG_CHIP_ID, chip_id); err != ESP_OK) {
 *       return err;
 *     }
 *     return (chip_id == EXPECTED_CHIP_ID) ? ESP_OK : ESP_ERR_NOT_FOUND;
 *   }
 *
 * private:
 *   static constexpr uint8_t REG_CHIP_ID = 0xD0;
 *   static constexpr uint8_t EXPECTED_CHIP_ID = 0x60;
 * };
 * @endcode
 */
class DeviceBase {
public:
  virtual ~DeviceBase() = default;

  DeviceBase(const DeviceBase &) = delete;
  DeviceBase &operator=(const DeviceBase &) = delete;
  DeviceBase(DeviceBase &&) = default;
  DeviceBase &operator=(DeviceBase &&) = default;

  /// Check if device is connected and ready
  [[nodiscard]] bool is_connected() const {
    return device_ != nullptr && device_->valid();
  }

  /// Get device I2C address
  [[nodiscard]] uint16_t address() const { return address_; }

protected:
  /**
   * @brief Construct device and register with master bus
   * @param bus I2C master bus
   * @param address Device I2C address
   * @param freq_hz Optional clock frequency override
   */
  DeviceBase(IMaster &bus, uint16_t address, uint32_t freq_hz = DEFAULT_FREQ_HZ)
      : device_(bus.create_device(address, freq_hz)), address_(address) {}

  /// @name Low-level I2C operations
  /// @{

  [[nodiscard]] esp_err_t write(std::span<const uint8_t> data,
                                Timeout timeout = FOREVER) {
    if (!is_connected()) {
      return ESP_ERR_INVALID_STATE;
    }
    return device_->write(data, timeout);
  }

  [[nodiscard]] esp_err_t read(std::span<uint8_t> buffer,
                               Timeout timeout = FOREVER) {
    if (!is_connected()) {
      return ESP_ERR_INVALID_STATE;
    }
    return device_->read(buffer, timeout);
  }

  [[nodiscard]] esp_err_t write_read(std::span<const uint8_t> tx_data,
                                     std::span<uint8_t> rx_buffer,
                                     Timeout timeout = FOREVER) {
    if (!is_connected()) {
      return ESP_ERR_INVALID_STATE;
    }
    return device_->write_read(tx_data, rx_buffer, timeout);
  }

  /// @}

  /// @name Register operations
  /// @{

  /// Read from a register
  [[nodiscard]] esp_err_t read_register(uint8_t reg, std::span<uint8_t> buffer,
                                        Timeout timeout = FOREVER) {
    return write_read(std::span(&reg, 1), buffer, timeout);
  }

  /// Read single byte from register
  [[nodiscard]] esp_err_t read_register(uint8_t reg, uint8_t &value,
                                        Timeout timeout = FOREVER) {
    return read_register(reg, std::span(&value, 1), timeout);
  }

  /// Read 16-bit value from register (big-endian)
  [[nodiscard]] esp_err_t read_register_be16(uint8_t reg, uint16_t &value,
                                             Timeout timeout = FOREVER) {
    std::array<uint8_t, 2> buf{};
    if (auto err = read_register(reg, buf, timeout); err != ESP_OK) {
      return err;
    }
    value = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
    return ESP_OK;
  }

  /// Read 16-bit value from register (little-endian)
  [[nodiscard]] esp_err_t read_register_le16(uint8_t reg, uint16_t &value,
                                             Timeout timeout = FOREVER) {
    std::array<uint8_t, 2> buf{};
    if (auto err = read_register(reg, buf, timeout); err != ESP_OK) {
      return err;
    }
    value = static_cast<uint16_t>((buf[1] << 8) | buf[0]);
    return ESP_OK;
  }

  /// Write to a register
  [[nodiscard]] esp_err_t write_register(uint8_t reg,
                                         std::span<const uint8_t> data,
                                         Timeout timeout = FOREVER) {
    if (!is_connected()) {
      return ESP_ERR_INVALID_STATE;
    }
    // Build buffer: [reg, data...]
    std::vector<uint8_t> buf;
    buf.reserve(1 + data.size());
    buf.push_back(reg);
    buf.insert(buf.end(), data.begin(), data.end());
    return device_->write(buf, timeout);
  }

  /// Write single byte to register
  [[nodiscard]] esp_err_t write_register(uint8_t reg, uint8_t value,
                                         Timeout timeout = FOREVER) {
    if (!is_connected()) {
      return ESP_ERR_INVALID_STATE;
    }
    std::array<uint8_t, 2> buf = {reg, value};
    return device_->write(buf, timeout);
  }

  /// @}

  /// @name Bit manipulation helpers
  /// @{

  /// Read, modify, write a register
  [[nodiscard]] esp_err_t modify_register(uint8_t reg, uint8_t mask,
                                          uint8_t value,
                                          Timeout timeout = FOREVER) {
    uint8_t current = 0;
    if (auto err = read_register(reg, current, timeout); err != ESP_OK) {
      return err;
    }
    uint8_t modified = (current & ~mask) | (value & mask);
    return write_register(reg, modified, timeout);
  }

  /// Set bits in a register
  [[nodiscard]] esp_err_t set_bits(uint8_t reg, uint8_t bits,
                                   Timeout timeout = FOREVER) {
    return modify_register(reg, bits, bits, timeout);
  }

  /// Clear bits in a register
  [[nodiscard]] esp_err_t clear_bits(uint8_t reg, uint8_t bits,
                                     Timeout timeout = FOREVER) {
    return modify_register(reg, bits, 0, timeout);
  }

  /// @}

private:
  std::unique_ptr<IDevice> device_;
  uint16_t address_;
};

} // namespace driver::i2c
