/**
 * @file driver.hpp
 * @brief Generic VFS-style driver interface
 */

#pragma once

#include <core/result.hpp>

#include <any>
#include <cstdint>
#include <span>

namespace driver {

/**
 * @brief Generic driver interface
 *
 * Drivers are self-managing: operations on a closed driver return
 * ESP_ERR_INVALID_STATE.
 */
class IDriver {
public:
  virtual ~IDriver() = default;

  IDriver(const IDriver &) = delete;
  IDriver &operator=(const IDriver &) = delete;
  IDriver(IDriver &&) = default;
  IDriver &operator=(IDriver &&) = default;

  /// Open the driver
  [[nodiscard]] virtual core::Status open() = 0;

  /// Close the driver
  [[nodiscard]] virtual core::Status close() = 0;

  /// Read data from device
  [[nodiscard]] virtual core::Result<size_t>
  read(std::span<uint8_t> buffer) = 0;

  /// Write data to device
  [[nodiscard]] virtual core::Result<size_t>
  write(std::span<const uint8_t> data) = 0;

  /// Device-specific control operations
  [[nodiscard]] virtual core::Result<std::any> ioctl(uint32_t cmd,
                                                     std::any arg) = 0;

protected:
  IDriver() = default;
};

} // namespace driver
