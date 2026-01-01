/**
 * @file master.hpp
 * @brief RAII I2C master driver implementing the I2C interfaces
 */

#pragma once

#include "interface.hpp"
#include "types.hpp"

#include <driver/i2c_master.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace driver::i2c {

/// Forward declaration
class Master;

/**
 * @brief RAII I2C device handle
 *
 * Represents a device on an I2C bus. Automatically deregisters
 * from the bus when destroyed.
 */
class Device final : public IDevice {
public:
  Device() = default;

  ~Device() override {
    if (handle_ != nullptr) {
      i2c_master_bus_rm_device(handle_);
    }
  }

  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;

  Device(Device &&other) noexcept : handle_(other.handle_), addr_(other.addr_) {
    other.handle_ = nullptr;
  }

  Device &operator=(Device &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        i2c_master_bus_rm_device(handle_);
      }
      handle_ = other.handle_;
      addr_ = other.addr_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Write data to device
  [[nodiscard]] esp_err_t write(std::span<const uint8_t> data,
                                Timeout timeout = FOREVER) override {
    return i2c_master_transmit(handle_, data.data(), data.size(),
                               to_ms(timeout));
  }

  /// Read data from device
  [[nodiscard]] esp_err_t read(std::span<uint8_t> buffer,
                               Timeout timeout = FOREVER) override {
    return i2c_master_receive(handle_, buffer.data(), buffer.size(),
                              to_ms(timeout));
  }

  /// Write then read (combined transaction)
  [[nodiscard]] esp_err_t write_read(std::span<const uint8_t> tx_data,
                                     std::span<uint8_t> rx_buffer,
                                     Timeout timeout = FOREVER) override {
    return i2c_master_transmit_receive(handle_, tx_data.data(), tx_data.size(),
                                       rx_buffer.data(), rx_buffer.size(),
                                       to_ms(timeout));
  }

  /// Write single byte
  [[nodiscard]] esp_err_t write_byte(uint8_t byte, Timeout timeout = FOREVER) {
    return write(std::span(&byte, 1), timeout);
  }

  /// Read from register
  [[nodiscard]] esp_err_t read_register(uint8_t reg, std::span<uint8_t> buffer,
                                        Timeout timeout = FOREVER) {
    return write_read(std::span(&reg, 1), buffer, timeout);
  }

  /// Read single byte from register
  [[nodiscard]] esp_err_t read_register_byte(uint8_t reg, uint8_t &out,
                                             Timeout timeout = FOREVER) {
    return read_register(reg, std::span(&out, 1), timeout);
  }

  /// Write to register (address + data)
  [[nodiscard]] esp_err_t write_register(uint8_t reg,
                                         std::span<const uint8_t> data,
                                         Timeout timeout = FOREVER) {
    std::vector<uint8_t> buf;
    buf.reserve(1 + data.size());
    buf.push_back(reg);
    buf.insert(buf.end(), data.begin(), data.end());
    return write(buf, timeout);
  }

  /// Write single byte to register
  [[nodiscard]] esp_err_t write_register_byte(uint8_t reg, uint8_t value,
                                              Timeout timeout = FOREVER) {
    std::array<uint8_t, 2> buf = {reg, value};
    return write(buf, timeout);
  }

  [[nodiscard]] bool valid() const override { return handle_ != nullptr; }
  [[nodiscard]] explicit operator bool() const { return valid(); }

  [[nodiscard]] uint16_t address() const override { return addr_; }
  [[nodiscard]] i2c_master_dev_handle_t native_handle() const {
    return handle_;
  }

private:
  friend class Master;

  Device(i2c_master_dev_handle_t handle, uint16_t addr)
      : handle_(handle), addr_(addr) {}

  i2c_master_dev_handle_t handle_ = nullptr;
  uint16_t addr_ = 0;
};

/// I2C bus configuration
struct Config {
  gpio_num_t sda_pin{};
  gpio_num_t scl_pin{};
  i2c_port_num_t port = I2C_NUM_0;
  uint32_t freq_hz = DEFAULT_FREQ_HZ;
  bool enable_internal_pullup = true;
  uint8_t glitch_ignore_cnt = 7;
};

/**
 * @brief RAII I2C master bus
 *
 * Manages an I2C master bus and allows adding devices.
 * Automatically deinitializes the bus when destroyed.
 */
class Master final : public IMaster {
public:
  explicit Master(const Config &config) : freq_hz_(config.freq_hz) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = config.port,
        .sda_io_num = config.sda_pin,
        .scl_io_num = config.scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = config.glitch_ignore_cnt,
        .intr_priority = 0,
        .trans_queue_depth = 0, // Synchronous mode
        .flags =
            {
                .enable_internal_pullup =
                    static_cast<uint32_t>(config.enable_internal_pullup),
                .allow_pd = 0,
            },
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &handle_);
    assert(err == ESP_OK && "Failed to create I2C master bus");
  }

  ~Master() override {
    if (handle_ != nullptr) {
      i2c_del_master_bus(handle_);
    }
  }

  Master(const Master &) = delete;
  Master &operator=(const Master &) = delete;

  Master(Master &&other) noexcept
      : handle_(other.handle_), freq_hz_(other.freq_hz_) {
    other.handle_ = nullptr;
  }

  Master &operator=(Master &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        i2c_del_master_bus(handle_);
      }
      handle_ = other.handle_;
      freq_hz_ = other.freq_hz_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Create a device on this bus (returns interface pointer)
  [[nodiscard]] std::unique_ptr<IDevice>
  create_device(uint16_t address, uint32_t freq_hz = DEFAULT_FREQ_HZ) override {
    auto device = add_device(address, freq_hz);
    if (!device.valid()) {
      return nullptr;
    }
    return std::make_unique<Device>(std::move(device));
  }

  /// Add a device on this bus (returns concrete type)
  [[nodiscard]] Device add_device(uint16_t address, uint32_t freq_hz = 0) {
    if (freq_hz == 0) {
      freq_hz = freq_hz_;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = freq_hz,
        .scl_wait_us = 0,
        .flags =
            {
                .disable_ack_check = static_cast<uint32_t>(false),
            },
    };

    i2c_master_dev_handle_t dev_handle = nullptr;
    esp_err_t err =
        i2c_master_bus_add_device(handle_, &dev_config, &dev_handle);
    if (err != ESP_OK) {
      return {};
    }

    return {dev_handle, address};
  }

  /// Probe for device at address (check if it ACKs)
  [[nodiscard]] bool probe(uint16_t address,
                           Timeout timeout = DEFAULT_PROBE_TIMEOUT) override {
    return i2c_master_probe(handle_, address, to_ms(timeout)) == ESP_OK;
  }

  /// Scan bus and return list of responding addresses
  [[nodiscard]] std::vector<uint16_t> scan(uint16_t start = 0x08,
                                           uint16_t end = 0x77) {
    std::vector<uint16_t> found;
    for (uint16_t addr = start; addr <= end; ++addr) {
      if (probe(addr)) {
        found.push_back(addr);
      }
    }
    return found;
  }

  [[nodiscard]] bool valid() const override { return handle_ != nullptr; }
  [[nodiscard]] explicit operator bool() const { return valid(); }

  [[nodiscard]] i2c_master_bus_handle_t native_handle() const {
    return handle_;
  }

private:
  i2c_master_bus_handle_t handle_ = nullptr;
  uint32_t freq_hz_;
};

} // namespace driver::i2c
