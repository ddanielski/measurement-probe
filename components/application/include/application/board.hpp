/**
 * @file board.hpp
 * @brief Board hardware abstraction
 *
 * Owns and initializes all hardware peripherals for this specific board.
 * This is application-specific (not a reusable library component).
 */

#pragma once

#include <i2c/i2c.hpp>

#include <memory>

namespace application {

/// Board hardware configuration
struct BoardConfig {
  gpio_num_t i2c_sda = GPIO_NUM_NC;
  gpio_num_t i2c_scl = GPIO_NUM_NC;
  uint32_t i2c_freq_hz = 100000;
};

/// Board hardware abstraction - owns all peripherals
class Board {
public:
  /// Construct board with configuration
  explicit Board(const BoardConfig &config);

  ~Board() = default;

  Board(const Board &) = delete;
  Board &operator=(const Board &) = delete;
  Board(Board &&) = default;
  Board &operator=(Board &&) = default;

  /// Check if board initialized successfully
  [[nodiscard]] bool valid() const { return valid_; }

  /// Get I2C bus (for sensors)
  [[nodiscard]] driver::i2c::Master &i2c() { return *i2c_; }
  [[nodiscard]] const driver::i2c::Master &i2c() const { return *i2c_; }

private:
  bool valid_ = false;
  std::unique_ptr<driver::i2c::Master> i2c_;
};

} // namespace application
