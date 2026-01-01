/**
 * @file mutex.hpp
 * @brief RAII mutex wrapper for FreeRTOS
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cassert>
#include <chrono>
#include <mutex>

namespace core {

/// RAII mutex wrapper
class Mutex {
public:
  Mutex() : handle_(xSemaphoreCreateMutex()) {
    assert(handle_ != nullptr && "Failed to create mutex");
  }

  ~Mutex() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
    }
  }

  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;

  Mutex(Mutex &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  Mutex &operator=(Mutex &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Try to lock, returns true if acquired
  [[nodiscard]] bool try_lock() { return xSemaphoreTake(handle_, 0) == pdTRUE; }

  /// Lock with timeout, returns true if acquired
  template <typename Rep, typename Period>
  [[nodiscard]] bool try_lock_for(std::chrono::duration<Rep, Period> timeout) {
    auto ticks = pdMS_TO_TICKS(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    return xSemaphoreTake(handle_, ticks) == pdTRUE;
  }

  /// Lock indefinitely
  void lock() {
    [[maybe_unused]] auto result = xSemaphoreTake(handle_, portMAX_DELAY);
    assert(result == pdTRUE);
  }

  /// Unlock
  void unlock() {
    [[maybe_unused]] auto result = xSemaphoreGive(handle_);
    assert(result == pdTRUE);
  }

  /// Get raw handle
  [[nodiscard]] SemaphoreHandle_t native_handle() const { return handle_; }

private:
  SemaphoreHandle_t handle_;
};

/// Recursive mutex (can be locked multiple times by same task)
class RecursiveMutex {
public:
  RecursiveMutex() : handle_(xSemaphoreCreateRecursiveMutex()) {
    assert(handle_ != nullptr && "Failed to create recursive mutex");
  }

  ~RecursiveMutex() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
    }
  }

  RecursiveMutex(const RecursiveMutex &) = delete;
  RecursiveMutex &operator=(const RecursiveMutex &) = delete;

  RecursiveMutex(RecursiveMutex &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  RecursiveMutex &operator=(RecursiveMutex &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool try_lock() {
    return xSemaphoreTakeRecursive(handle_, 0) == pdTRUE;
  }

  template <typename Rep, typename Period>
  [[nodiscard]] bool try_lock_for(std::chrono::duration<Rep, Period> timeout) {
    auto ticks = pdMS_TO_TICKS(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    return xSemaphoreTakeRecursive(handle_, ticks) == pdTRUE;
  }

  void lock() {
    [[maybe_unused]] auto result =
        xSemaphoreTakeRecursive(handle_, portMAX_DELAY);
    assert(result == pdTRUE);
  }

  void unlock() {
    [[maybe_unused]] auto result = xSemaphoreGiveRecursive(handle_);
    assert(result == pdTRUE);
  }

  [[nodiscard]] SemaphoreHandle_t native_handle() const { return handle_; }

private:
  SemaphoreHandle_t handle_;
};

/// RAII lock guard (like std::lock_guard)
template <typename MutexType> class LockGuard {
public:
  explicit LockGuard(MutexType &mutex) : mutex_(mutex) { mutex_.lock(); }

  ~LockGuard() { mutex_.unlock(); }

  LockGuard(const LockGuard &) = delete;
  LockGuard &operator=(const LockGuard &) = delete;
  LockGuard(LockGuard &&) = delete;
  LockGuard &operator=(LockGuard &&) = delete;

private:
  MutexType &mutex_;
};

/// RAII unique lock (like std::unique_lock, supports try_lock and deferred
/// locking)
template <typename MutexType> class UniqueLock {
public:
  explicit UniqueLock(MutexType &mutex) : mutex_(&mutex), owned_(false) {
    lock();
  }

  UniqueLock(MutexType &mutex, std::defer_lock_t)
      : mutex_(&mutex), owned_(false) {}

  UniqueLock(MutexType &mutex, std::try_to_lock_t)
      : mutex_(&mutex), owned_(mutex.try_lock()) {}

  ~UniqueLock() {
    if (owned_) {
      mutex_->unlock();
    }
  }

  UniqueLock(const UniqueLock &) = delete;
  UniqueLock &operator=(const UniqueLock &) = delete;

  UniqueLock(UniqueLock &&other) noexcept
      : mutex_(other.mutex_), owned_(other.owned_) {
    other.mutex_ = nullptr;
    other.owned_ = false;
  }

  UniqueLock &operator=(UniqueLock &&other) noexcept {
    if (this != &other) {
      if (owned_) {
        mutex_->unlock();
      }
      mutex_ = other.mutex_;
      owned_ = other.owned_;
      other.mutex_ = nullptr;
      other.owned_ = false;
    }
    return *this;
  }

  void lock() {
    assert(mutex_ != nullptr && !owned_);
    mutex_->lock();
    owned_ = true;
  }

  [[nodiscard]] bool try_lock() {
    assert(mutex_ != nullptr && !owned_);
    owned_ = mutex_->try_lock();
    return owned_;
  }

  template <typename Rep, typename Period>
  [[nodiscard]] bool try_lock_for(std::chrono::duration<Rep, Period> timeout) {
    assert(mutex_ != nullptr && !owned_);
    owned_ = mutex_->try_lock_for(timeout);
    return owned_;
  }

  void unlock() {
    assert(mutex_ != nullptr && owned_);
    mutex_->unlock();
    owned_ = false;
  }

  [[nodiscard]] bool owns_lock() const { return owned_; }
  [[nodiscard]] explicit operator bool() const { return owned_; }

  MutexType *release() {
    auto *m = mutex_;
    mutex_ = nullptr;
    owned_ = false;
    return m;
  }

private:
  MutexType *mutex_;
  bool owned_;
};

} // namespace core
