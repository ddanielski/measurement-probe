/**
 * @file result.hpp
 * @brief Result type wrapping esp_err_t
 */

#pragma once

#include <esp_err.h>

#include <type_traits>
#include <utility>

namespace core {

/// Result type for failable operations
template <typename T> class Result {
public:
  Result(const T &value) : value_(value), error_(ESP_OK) {}
  Result(T &&value) : value_(std::move(value)), error_(ESP_OK) {}

  Result(esp_err_t err) : value_{}, error_(err) {
    if (err == ESP_OK)
      error_ = ESP_FAIL;
  }

  [[nodiscard]] bool ok() const noexcept { return error_ == ESP_OK; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] esp_err_t error() const noexcept { return error_; }

  [[nodiscard]] T &value() & noexcept { return value_; }
  [[nodiscard]] const T &value() const & noexcept { return value_; }
  [[nodiscard]] T &&value() && noexcept { return std::move(value_); }

  [[nodiscard]] T *operator->() noexcept { return &value_; }
  [[nodiscard]] const T *operator->() const noexcept { return &value_; }
  [[nodiscard]] T &operator*() & noexcept { return value_; }
  [[nodiscard]] const T &operator*() const & noexcept { return value_; }
  [[nodiscard]] T &&operator*() && noexcept { return std::move(value_); }

  template <typename U> [[nodiscard]] T value_or(U &&default_val) const & {
    return ok() ? value_ : static_cast<T>(std::forward<U>(default_val));
  }

private:
  T value_;
  esp_err_t error_;
};

/// Specialization for void
template <> class Result<void> {
public:
  Result() : error_(ESP_OK) {}
  Result(esp_err_t err) : error_(err) {}

  [[nodiscard]] bool ok() const noexcept { return error_ == ESP_OK; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] esp_err_t error() const noexcept { return error_; }

private:
  esp_err_t error_;
};

using Status = Result<void>;

} // namespace core
