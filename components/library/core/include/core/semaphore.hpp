/**
 * @file semaphore.hpp
 * @brief RAII semaphore wrappers for FreeRTOS
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cassert>
#include <chrono>
#include <cstdint>

namespace core {

/// Binary semaphore (0 or 1)
class BinarySemaphore {
public:
  BinarySemaphore() : handle_(xSemaphoreCreateBinary()) {
    assert(handle_ != nullptr && "Failed to create binary semaphore");
  }

  ~BinarySemaphore() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
    }
  }

  BinarySemaphore(const BinarySemaphore &) = delete;
  BinarySemaphore &operator=(const BinarySemaphore &) = delete;

  BinarySemaphore(BinarySemaphore &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  BinarySemaphore &operator=(BinarySemaphore &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Take (wait/acquire), returns true if acquired
  [[nodiscard]] bool take() {
    return xSemaphoreTake(handle_, portMAX_DELAY) == pdTRUE;
  }

  /// Try to take without waiting
  [[nodiscard]] bool try_take() { return xSemaphoreTake(handle_, 0) == pdTRUE; }

  /// Take with timeout
  template <typename Rep, typename Period>
  [[nodiscard]] bool take_for(std::chrono::duration<Rep, Period> timeout) {
    auto ticks = pdMS_TO_TICKS(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    return xSemaphoreTake(handle_, ticks) == pdTRUE;
  }

  /// Give (signal/release)
  void give() { xSemaphoreGive(handle_); }

  /// Give from ISR
  bool give_from_isr() {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(handle_, &woken);
    return woken == pdTRUE;
  }

  [[nodiscard]] SemaphoreHandle_t native_handle() const { return handle_; }

private:
  SemaphoreHandle_t handle_;
};

/// Counting semaphore
class CountingSemaphore {
public:
  CountingSemaphore(uint32_t max_count, uint32_t initial_count = 0)
      : handle_(xSemaphoreCreateCounting(max_count, initial_count)) {
    assert(handle_ != nullptr && "Failed to create counting semaphore");
  }

  ~CountingSemaphore() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
    }
  }

  CountingSemaphore(const CountingSemaphore &) = delete;
  CountingSemaphore &operator=(const CountingSemaphore &) = delete;

  CountingSemaphore(CountingSemaphore &&other) noexcept
      : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  CountingSemaphore &operator=(CountingSemaphore &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool take() {
    return xSemaphoreTake(handle_, portMAX_DELAY) == pdTRUE;
  }

  [[nodiscard]] bool try_take() { return xSemaphoreTake(handle_, 0) == pdTRUE; }

  template <typename Rep, typename Period>
  [[nodiscard]] bool take_for(std::chrono::duration<Rep, Period> timeout) {
    auto ticks = pdMS_TO_TICKS(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    return xSemaphoreTake(handle_, ticks) == pdTRUE;
  }

  void give() { xSemaphoreGive(handle_); }

  bool give_from_isr() {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(handle_, &woken);
    return woken == pdTRUE;
  }

  /// Get current count
  [[nodiscard]] uint32_t count() const { return uxSemaphoreGetCount(handle_); }

  [[nodiscard]] SemaphoreHandle_t native_handle() const { return handle_; }

private:
  SemaphoreHandle_t handle_;
};

} // namespace core
