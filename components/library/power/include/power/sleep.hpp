/**
 * @file sleep.hpp
 * @brief Sleep management with proper safety
 */

#pragma once

#include <esp_sleep.h>

#include <chrono>

namespace power {

namespace limits {
constexpr auto MAX_SLEEP_DURATION = std::chrono::hours(24);
constexpr auto MAX_SLEEP_SECONDS =
    std::chrono::duration_cast<std::chrono::seconds>(MAX_SLEEP_DURATION)
        .count();
} // namespace limits

enum class WakeReason : uint8_t {
  PowerOn,
  Timer,
  Gpio,
  Other,
};

[[nodiscard]] inline WakeReason get_wake_reason() {
  switch (esp_sleep_get_wakeup_cause()) {
  case ESP_SLEEP_WAKEUP_TIMER:
    return WakeReason::Timer;
  case ESP_SLEEP_WAKEUP_GPIO:
    return WakeReason::Gpio;
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return WakeReason::PowerOn;
  default:
    return WakeReason::Other;
  }
}

[[nodiscard]] inline const char *to_string(WakeReason reason) {
  switch (reason) {
  case WakeReason::PowerOn:
    return "power-on/reset";
  case WakeReason::Timer:
    return "timer";
  case WakeReason::Gpio:
    return "GPIO";
  default:
    return "other";
  }
}

class DeepSleep {
public:
  using Duration = std::chrono::seconds;

  template <typename Rep, typename Period>
  explicit constexpr DeepSleep(std::chrono::duration<Rep, Period> interval)
      : interval_(std::chrono::duration_cast<Duration>(interval)) {
    static_assert(std::chrono::duration_cast<Duration>(
                      std::chrono::duration<Rep, Period>::max())
                          .count() > 0,
                  "Duration type must support positive values");
    // Runtime checks for values not known at compile time
    if (interval_.count() <= 0) {
      interval_ = Duration{1}; // Minimum 1 second
    }
    if (interval_.count() > limits::MAX_SLEEP_SECONDS) {
      interval_ = Duration{limits::MAX_SLEEP_SECONDS};
    }
  }

  [[noreturn]] void enter() const {
    uint64_t us =
        std::chrono::duration_cast<std::chrono::microseconds>(interval_)
            .count();
    esp_sleep_enable_timer_wakeup(us);
    esp_deep_sleep_start();
    __builtin_unreachable();
  }

  [[nodiscard]] constexpr Duration interval() const { return interval_; }

private:
  Duration interval_;
};

} // namespace power
