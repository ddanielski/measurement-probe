/**
 * @file crc.hpp
 * @brief Modern C++ wrapper for ESP ROM CRC functions
 *
 * Provides type-safe CRC32 computation with:
 * - Span-based API for buffer safety
 * - Constrained templates for type safety
 * - Incremental hashing support
 * - Seed customization to avoid trivial outputs
 */

#pragma once

#include <esp_rom_crc.h>

#include <concepts>
#include <cstdint>
#include <span>
#include <type_traits>

namespace core {

/// Concept for types that can be hashed via CRC
template <typename T>
concept CrcHashable = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

/// CRC32 hasher with configurable seed
///
/// The seed ensures that:
/// - All-zero input doesn't produce zero output
/// - All-one input doesn't produce 0xFFFFFFFF
///
/// Usage:
///   Crc32 crc;
///   crc.update(data);
///   uint32_t checksum = crc.value();
///
/// Or single-shot:
///   uint32_t checksum = Crc32::compute(data);
///
class Crc32 {
public:
  /// Default seed chosen to avoid trivial outputs
  /// CRC32 of "RTC!" = 0x9E83B3D1 - produces non-trivial results for edge cases
  static constexpr uint32_t DEFAULT_SEED = 0x9E83B3D1;

  /// Create hasher with default seed
  constexpr Crc32() = default;

  /// Create hasher with custom seed
  constexpr explicit Crc32(uint32_t seed) : state_(seed) {}

  /// Update CRC with raw bytes
  void update(std::span<const uint8_t> data) {
    state_ = esp_rom_crc32_le(state_, data.data(),
                              static_cast<uint32_t>(data.size()));
  }

  /// Update CRC with any trivially copyable type
  template <CrcHashable T> void update(const T &value) {
    update(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&value),
                                    sizeof(T)));
  }

  /// Get current CRC value
  [[nodiscard]] constexpr uint32_t value() const { return state_; }

  /// Reset to initial state
  constexpr void reset() { state_ = DEFAULT_SEED; }

  /// Reset with new seed
  constexpr void reset(uint32_t seed) { state_ = seed; }

  // --- Static convenience methods ---

  /// Compute CRC of raw bytes (single-shot)
  [[nodiscard]] static uint32_t compute(std::span<const uint8_t> data) {
    Crc32 crc;
    crc.update(data);
    return crc.value();
  }

  /// Compute CRC of any trivially copyable value (single-shot)
  template <CrcHashable T>
  [[nodiscard]] static uint32_t compute(const T &value) {
    Crc32 crc;
    crc.update(value);
    return crc.value();
  }

  /// Compute CRC of multiple values (single-shot, variadic)
  template <CrcHashable... Ts>
  [[nodiscard]] static uint32_t compute(const Ts &...values) {
    Crc32 crc;
    (crc.update(values), ...);
    return crc.value();
  }

private:
  uint32_t state_{DEFAULT_SEED};
};

} // namespace core
