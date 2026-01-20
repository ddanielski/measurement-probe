/**
 * @file rtc_storage.hpp
 * @brief RTC memory storage for data that survives deep sleep
 *
 * RTC memory is preserved during deep sleep but lost on reset/power cycle.
 * Use for:
 * - Authentication tokens (refresh on cold boot)
 * - Wake cycle counters
 * - Temporary state between sleep cycles
 *
 * ESP32-C3 has ~8KB of RTC SLOW memory available for user data.
 *
 * Data integrity is validated using CRC32 over the payload.
 */

#pragma once

#include "crc.hpp"

#include <esp_attr.h>
#include <esp_sleep.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace core {

namespace rtc {

/// Maximum size for variable-length RTC data (tokens, strings)
/// JWT tokens from cloud providers are typically 1000-1500 bytes
inline constexpr size_t MAX_VAR_DATA_SIZE = 2048;

} // namespace rtc

/// RTC-stored string with CRC validation
struct RtcString {
  uint32_t crc{0};
  uint16_t length{0};
  std::array<char, rtc::MAX_VAR_DATA_SIZE> data{};

  [[nodiscard]] bool is_valid() const {
    return length > 0 && crc == Crc32::compute(length, as_bytes());
  }

  [[nodiscard]] std::string_view view() const {
    return is_valid() ? std::string_view{data.data(), length}
                      : std::string_view{};
  }

  void set(std::string_view str) {
    length = static_cast<uint16_t>(std::min(str.size(), data.size() - 1));
    std::copy_n(str.data(), length, data.data());
    data.at(length) = '\0';
    crc = Crc32::compute(length, as_bytes());
  }

  void clear() {
    crc = 0;
    length = 0;
  }

private:
  [[nodiscard]] std::span<const uint8_t> as_bytes() const {
    return {reinterpret_cast<const uint8_t *>(data.data()), length};
  }
};

/// RTC-stored blob with CRC validation
struct RtcBlob {
  uint32_t crc{0};
  uint16_t length{0};
  std::array<uint8_t, rtc::MAX_VAR_DATA_SIZE> data{};

  [[nodiscard]] bool is_valid() const {
    return length > 0 && crc == Crc32::compute(length, as_bytes());
  }

  [[nodiscard]] std::span<const uint8_t> span() const {
    return is_valid() ? as_bytes() : std::span<const uint8_t>{};
  }

  void set(std::span<const uint8_t> blob) {
    length = static_cast<uint16_t>(std::min(blob.size(), data.size()));
    std::copy_n(blob.data(), length, data.data());
    crc = Crc32::compute(length, as_bytes());
  }

  void clear() {
    crc = 0;
    length = 0;
  }

private:
  [[nodiscard]] std::span<const uint8_t> as_bytes() const {
    return {data.data(), length};
  }
};

/// RTC-stored scalar value with CRC validation
template <CrcHashable T> struct RtcValue {
  uint32_t crc{0};
  T value{};

  [[nodiscard]] bool is_valid() const { return crc == Crc32::compute(value); }

  [[nodiscard]] T get(T default_value = {}) const {
    return is_valid() ? value : default_value;
  }

  void set(T v) {
    value = v;
    crc = Crc32::compute(value);
  }

  void clear() { crc = 0; }
};

/// RTC-stored timestamp with CRC validation
struct RtcTimestamp {
  uint32_t crc{0};
  int64_t epoch_ms{0};

  [[nodiscard]] bool is_valid() const {
    return crc == Crc32::compute(epoch_ms);
  }

  [[nodiscard]] std::chrono::system_clock::time_point get() const {
    if (!is_valid()) {
      return {};
    }
    return std::chrono::system_clock::time_point{
        std::chrono::milliseconds{epoch_ms}};
  }

  void set(std::chrono::system_clock::time_point tp) {
    epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   tp.time_since_epoch())
                   .count();
    crc = Crc32::compute(epoch_ms);
  }

  void clear() { crc = 0; }
};

/// Authentication token storage for RTC memory
struct RtcAuthToken {
  RtcString token;
  RtcTimestamp expires_at;

  [[nodiscard]] bool is_valid() const {
    if (!token.is_valid()) {
      return false;
    }
    if (!expires_at.is_valid()) {
      return true; // No expiry set, assume valid
    }
    return std::chrono::system_clock::now() < expires_at.get();
  }

  template <typename Rep, typename Period>
  [[nodiscard]] bool
  needs_refresh(std::chrono::duration<Rep, Period> buffer) const {
    if (!token.is_valid()) {
      return true;
    }
    if (!expires_at.is_valid()) {
      return false;
    }
    return std::chrono::system_clock::now() >= (expires_at.get() - buffer);
  }

  void set(std::string_view tok,
           std::chrono::system_clock::time_point exp = {}) {
    token.set(tok);
    if (exp != std::chrono::system_clock::time_point{}) {
      expires_at.set(exp);
    } else {
      expires_at.clear();
    }
  }

  void clear() {
    token.clear();
    expires_at.clear();
  }
};

/// Check if device woke from deep sleep (vs cold boot)
[[nodiscard]] inline bool woke_from_deep_sleep() {
  return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
}

} // namespace core
