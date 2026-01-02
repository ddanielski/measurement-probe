/**
 * @file result.hpp
 * @brief Result type using std::expected for type-safe error handling
 *
 * Usage:
 *   Status foo() {
 *     if (error) return Err(ESP_FAIL);
 *     return Ok();
 *   }
 *
 *   Result<int> bar() {
 *     if (error) return Err(ESP_FAIL);
 *     return 42;  // or Ok(42)
 *   }
 */

#pragma once

#include <esp_err.h>

#include <expected>

namespace core {

/// Result type for failable operations - uses std::expected for type safety
template <typename T> using Result = std::expected<T, esp_err_t>;

/// Status type for operations that don't return a value
using Status = std::expected<void, esp_err_t>;

/// Create a success Status (void result)
[[nodiscard]] inline Status Ok() { return {}; }

/// Create a success Result with value
template <typename T> [[nodiscard]] Result<T> Ok(T &&value) {
  return std::forward<T>(value);
}

/// Create an error (works for both Status and Result<T>)
[[nodiscard]] inline std::unexpected<esp_err_t> Err(esp_err_t err) {
  return std::unexpected(err);
}

} // namespace core
