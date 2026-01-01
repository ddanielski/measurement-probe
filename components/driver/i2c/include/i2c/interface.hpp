/**
 * @file interface.hpp
 * @brief Abstract I2C interfaces for dependency injection and testing
 */

#pragma once

#include "types.hpp"

#include <esp_err.h>

#include <cstdint>
#include <memory>
#include <span>

namespace driver::i2c {

/// Abstract I2C device interface
class IDevice {
public:
  virtual ~IDevice() = default;

  IDevice(const IDevice &) = delete;
  IDevice &operator=(const IDevice &) = delete;
  IDevice(IDevice &&) = default;
  IDevice &operator=(IDevice &&) = default;

  /// Write data to device
  [[nodiscard]] virtual esp_err_t write(std::span<const uint8_t> data,
                                        Timeout timeout = FOREVER) = 0;

  /// Read data from device
  [[nodiscard]] virtual esp_err_t read(std::span<uint8_t> buffer,
                                       Timeout timeout = FOREVER) = 0;

  /// Write then read (combined transaction)
  [[nodiscard]] virtual esp_err_t write_read(std::span<const uint8_t> tx_data,
                                             std::span<uint8_t> rx_buffer,
                                             Timeout timeout = FOREVER) = 0;

  /// Check if device handle is valid
  [[nodiscard]] virtual bool valid() const = 0;

  /// Get device address
  [[nodiscard]] virtual uint16_t address() const = 0;

protected:
  IDevice() = default;
};

/// Abstract I2C master bus interface
class IMaster {
public:
  virtual ~IMaster() = default;

  IMaster(const IMaster &) = delete;
  IMaster &operator=(const IMaster &) = delete;
  IMaster(IMaster &&) = default;
  IMaster &operator=(IMaster &&) = default;

  /// Add a device to this bus
  [[nodiscard]] virtual std::unique_ptr<IDevice>
  create_device(uint16_t address, uint32_t freq_hz = DEFAULT_FREQ_HZ) = 0;

  /// Probe for device presence (check ACK)
  [[nodiscard]] virtual bool probe(uint16_t address,
                                   Timeout timeout = DEFAULT_PROBE_TIMEOUT) = 0;

  /// Check if bus handle is valid
  [[nodiscard]] virtual bool valid() const = 0;

protected:
  IMaster() = default;
};

} // namespace driver::i2c
