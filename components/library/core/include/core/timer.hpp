/**
 * @file timer.hpp
 * @brief RAII timer wrapper for esp_timer
 */

#pragma once

#include "result.hpp"

#include <esp_timer.h>

#include <cassert>
#include <chrono>
#include <functional>

namespace core {

/// Timer that fires once after a delay
class OneShotTimer {
public:
  using Callback = std::function<void()>;

  explicit OneShotTimer(Callback callback) : callback_(std::move(callback)) {
    esp_timer_create_args_t args = {
        .callback = timer_callback,
        .arg = this, // Pass 'this' instead of pointer to callback
        .dispatch_method = ESP_TIMER_TASK,
        .name = "oneshot",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&args, &handle_);
    assert(err == ESP_OK && "Failed to create timer");
  }

  ~OneShotTimer() {
    if (handle_ != nullptr) {
      esp_timer_stop(handle_);
      esp_timer_delete(handle_);
    }
  }

  OneShotTimer(const OneShotTimer &) = delete;
  OneShotTimer &operator=(const OneShotTimer &) = delete;

  OneShotTimer(OneShotTimer &&) = delete;
  OneShotTimer &operator=(OneShotTimer &&) = delete;

  /// Start timer with given delay
  template <typename Rep, typename Period>
  Status start(std::chrono::duration<Rep, Period> delay) {
    auto delay_us =
        std::chrono::duration_cast<std::chrono::microseconds>(delay).count();
    esp_err_t err = esp_timer_start_once(handle_, delay_us);
    if (err != ESP_OK) {
      return core::Err(err);
    }
    return core::Ok();
  }

  /// Stop timer if running
  Status stop() {
    esp_err_t err = esp_timer_stop(handle_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      return core::Err(err);
    }
    return core::Ok();
  }

  /// Check if timer is running
  [[nodiscard]] bool is_running() const { return esp_timer_is_active(handle_); }

  [[nodiscard]] esp_timer_handle_t native_handle() const { return handle_; }

private:
  static void timer_callback(void *arg) {
    auto *self = static_cast<OneShotTimer *>(arg);
    if (self != nullptr && static_cast<bool>(self->callback_)) {
      self->callback_();
    }
  }

  esp_timer_handle_t handle_ = nullptr;
  Callback callback_;
};

/// Timer that fires repeatedly
/// Memory: No heap allocation for timer management (callback stored inline)
class PeriodicTimer {
public:
  using Callback = std::function<void()>;

  explicit PeriodicTimer(Callback callback) : callback_(std::move(callback)) {
    esp_timer_create_args_t args = {
        .callback = timer_callback,
        .arg = this, // Pass 'this' instead of pointer to callback
        .dispatch_method = ESP_TIMER_TASK,
        .name = "periodic",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&args, &handle_);
    assert(err == ESP_OK && "Failed to create timer");
  }

  ~PeriodicTimer() {
    if (handle_ != nullptr) {
      esp_timer_stop(handle_);
      esp_timer_delete(handle_);
    }
  }

  PeriodicTimer(const PeriodicTimer &) = delete;
  PeriodicTimer &operator=(const PeriodicTimer &) = delete;

  PeriodicTimer(PeriodicTimer &&) = delete;
  PeriodicTimer &operator=(PeriodicTimer &&) = delete;

  /// Start timer with given period
  template <typename Rep, typename Period>
  Status start(std::chrono::duration<Rep, Period> period) {
    auto period_us =
        std::chrono::duration_cast<std::chrono::microseconds>(period).count();
    esp_err_t err = esp_timer_start_periodic(handle_, period_us);
    if (err != ESP_OK) {
      return core::Err(err);
    }
    return core::Ok();
  }

  /// Stop timer
  Status stop() {
    esp_err_t err = esp_timer_stop(handle_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      return core::Err(err);
    }
    return core::Ok();
  }

  /// Restart with new period
  template <typename Rep, typename Period>
  Status restart(std::chrono::duration<Rep, Period> period) {
    [[maybe_unused]] auto status = stop();
    return start(period);
  }

  [[nodiscard]] bool is_running() const { return esp_timer_is_active(handle_); }

  [[nodiscard]] esp_timer_handle_t native_handle() const { return handle_; }

private:
  static void timer_callback(void *arg) {
    auto *self = static_cast<PeriodicTimer *>(arg);
    if (self != nullptr && static_cast<bool>(self->callback_)) {
      self->callback_();
    }
  }

  esp_timer_handle_t handle_ = nullptr;
  Callback callback_;
};

} // namespace core
