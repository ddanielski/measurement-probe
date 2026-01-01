/**
 * @file types.hpp
 * @brief Common I2C types and constants
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace driver::i2c {

/// Timeout duration for I2C operations
using Timeout = std::chrono::milliseconds;

/// Wait forever (blocking)
inline constexpr Timeout FOREVER{-1};

/// Default timeout for probe operations
inline constexpr Timeout DEFAULT_PROBE_TIMEOUT{100};

/// Default I2C clock frequency (400 kHz / Fast Mode)
inline constexpr uint32_t DEFAULT_FREQ_HZ = 400'000;

/// Standard I2C clock frequency (100 kHz)
inline constexpr uint32_t STANDARD_FREQ_HZ = 100'000;

/// Fast Mode Plus I2C clock frequency (1 MHz)
inline constexpr uint32_t FAST_PLUS_FREQ_HZ = 1'000'000;

/// Convert Timeout to int (for ESP-IDF APIs)
[[nodiscard]] constexpr int to_ms(Timeout timeout) noexcept {
  return static_cast<int>(timeout.count());
}

} // namespace driver::i2c
