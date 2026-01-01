/**
 * @file task.hpp
 * @brief RAII task wrapper for FreeRTOS
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace core {

/// Task configuration
struct TaskConfig {
  const char *name = "default task";
  uint32_t stack_size = 4096;
  UBaseType_t priority = 5;
  BaseType_t core_id = tskNO_AFFINITY;
};

/// RAII task wrapper
class Task {
public:
  using Function = std::function<void()>;

  /// Create and start a task
  Task(Function func, const TaskConfig &config = {})
      : func_(std::make_unique<Function>(std::move(func))) {
    BaseType_t result = 0;
    if (config.core_id == tskNO_AFFINITY) {
      result = xTaskCreate(task_trampoline, config.name, config.stack_size,
                           func_.get(), config.priority, &handle_);
    } else {
      result = xTaskCreatePinnedToCore(
          task_trampoline, config.name, config.stack_size, func_.get(),
          config.priority, &handle_, config.core_id);
    }
    assert(result == pdPASS && "Failed to create task");
  }

  ~Task() {
    if (handle_ != nullptr) {
      vTaskDelete(handle_);
    }
  }

  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;

  Task(Task &&other) noexcept
      : handle_(other.handle_), func_(std::move(other.func_)) {
    other.handle_ = nullptr;
  }

  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        vTaskDelete(handle_);
      }
      handle_ = other.handle_;
      func_ = std::move(other.func_);
      other.handle_ = nullptr;
    }
    return *this;
  }

  /// Suspend the task
  void suspend() {
    if (handle_ != nullptr) {
      vTaskSuspend(handle_);
    }
  }

  /// Resume the task
  void resume() {
    if (handle_ != nullptr) {
      vTaskResume(handle_);
    }
  }

  /// Get task priority
  [[nodiscard]] UBaseType_t priority() const {
    return handle_ != nullptr ? uxTaskPriorityGet(handle_) : 0;
  }

  /// Set task priority
  void set_priority(UBaseType_t priority) {
    if (handle_ != nullptr) {
      vTaskPrioritySet(handle_, priority);
    }
  }

  /// Get task name
  [[nodiscard]] const char *name() const {
    return handle_ != nullptr ? pcTaskGetName(handle_) : nullptr;
  }

  /// Check if task handle is valid
  [[nodiscard]] bool valid() const { return handle_ != nullptr; }
  [[nodiscard]] explicit operator bool() const { return valid(); }

  [[nodiscard]] TaskHandle_t native_handle() const { return handle_; }

  /// Detach: release ownership, task continues running
  TaskHandle_t detach() {
    auto *h = handle_;
    handle_ = nullptr;
    (void)func_.release(); // NOLINT(bugprone-unused-return-value)
    return h;
  }

  // Static utilities

  /// Delay current task
  template <typename Rep, typename Period>
  static void delay(std::chrono::duration<Rep, Period> duration) {
    auto ticks = pdMS_TO_TICKS(
        std::chrono::duration_cast<std::chrono::milliseconds>(duration)
            .count());
    vTaskDelay(ticks);
  }

  /// Yield current task
  static void yield() { taskYIELD(); }

  /// Get current task handle
  [[nodiscard]] static TaskHandle_t current() {
    return xTaskGetCurrentTaskHandle();
  }

  /// Get free stack space of current task
  [[nodiscard]] static uint32_t stack_high_water_mark() {
    return uxTaskGetStackHighWaterMark(nullptr);
  }

private:
  static void task_trampoline(void *param) {
    auto *func = static_cast<Function *>(param);
    if (func != nullptr && static_cast<bool>(*func)) {
      (*func)();
    }
    vTaskDelete(nullptr);
  }

  TaskHandle_t handle_ = nullptr;
  std::unique_ptr<Function> func_;
};

} // namespace core
