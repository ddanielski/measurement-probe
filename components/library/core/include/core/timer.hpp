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
#include <memory>

namespace core {

/// Timer that fires once after a delay
class OneShotTimer {
public:
  using Callback = std::function<void()>;

  explicit OneShotTimer(Callback callback)
      : callback_(std::make_unique<Callback>(std::move(callback))) {
    esp_timer_create_args_t args = {
        .callback = timer_callback,
        .arg = callback_.get(),
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

  OneShotTimer(OneShotTimer &&other) noexcept
      : handle_(other.handle_), callback_(std::move(other.callback_)) {
    other.handle_ = nullptr;
  }

  OneShotTimer &operator=(OneShotTimer &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        esp_timer_stop(handle_);
        esp_timer_delete(handle_);
      }
      handle_ = other.handle_;
      callback_ = std::move(other.callback_);
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Start timer with given delay
  template <typename Rep, typename Period>
  Status start(std::chrono::duration<Rep, Period> delay) {
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(delay).count();
    return esp_timer_start_once(handle_, us);
  }

  /// Stop timer if running
  Status stop() { return esp_timer_stop(handle_); }

  /// Check if timer is running
  [[nodiscard]] bool is_running() const { return esp_timer_is_active(handle_); }

  [[nodiscard]] esp_timer_handle_t native_handle() const { return handle_; }

private:
  static void timer_callback(void *arg) {
    auto *cb = static_cast<Callback *>(arg);
    if (cb && *cb) {
      (*cb)();
    }
  }

  esp_timer_handle_t handle_ = nullptr;
  std::unique_ptr<Callback> callback_;
};

/// Timer that fires repeatedly
class PeriodicTimer {
public:
  using Callback = std::function<void()>;

  explicit PeriodicTimer(Callback callback)
      : callback_(std::make_unique<Callback>(std::move(callback))) {
    esp_timer_create_args_t args = {
        .callback = timer_callback,
        .arg = callback_.get(),
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

  PeriodicTimer(PeriodicTimer &&other) noexcept
      : handle_(other.handle_), callback_(std::move(other.callback_)) {
    other.handle_ = nullptr;
  }

  PeriodicTimer &operator=(PeriodicTimer &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        esp_timer_stop(handle_);
        esp_timer_delete(handle_);
      }
      handle_ = other.handle_;
      callback_ = std::move(other.callback_);
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Start timer with given period
  template <typename Rep, typename Period>
  Status start(std::chrono::duration<Rep, Period> period) {
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(period).count();
    return esp_timer_start_periodic(handle_, us);
  }

  /// Stop timer
  Status stop() { return esp_timer_stop(handle_); }

  /// Restart with new period
  template <typename Rep, typename Period>
  Status restart(std::chrono::duration<Rep, Period> period) {
    stop();
    return start(period);
  }

  [[nodiscard]] bool is_running() const { return esp_timer_is_active(handle_); }

  [[nodiscard]] esp_timer_handle_t native_handle() const { return handle_; }

private:
  static void timer_callback(void *arg) {
    auto *cb = static_cast<Callback *>(arg);
    if (cb && *cb) {
      (*cb)();
    }
  }

  esp_timer_handle_t handle_ = nullptr;
  std::unique_ptr<Callback> callback_;
};

} // namespace core
