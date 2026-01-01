/**
 * @file gpio.hpp
 * @brief Type-safe RAII GPIO wrapper
 */

#pragma once

#include "result.hpp"

#include <driver/gpio.h>

#include <cassert>
#include <cstdint>

namespace core {

/// GPIO pull mode
enum class Pull : uint8_t {
  None,
  Up,
  Down,
  UpDown, // Both (for I2C open-drain)
};

/// GPIO output pin (RAII)
class OutputPin {
public:
  explicit OutputPin(gpio_num_t pin, bool initial_level = false,
                     bool open_drain = false)
      : pin_(pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    assert(err == ESP_OK && "Failed to configure GPIO");
    gpio_set_level(pin_, initial_level ? 1 : 0);
  }

  ~OutputPin() { gpio_reset_pin(pin_); }

  OutputPin(const OutputPin &) = delete;
  OutputPin &operator=(const OutputPin &) = delete;
  OutputPin(OutputPin &&) = delete;
  OutputPin &operator=(OutputPin &&) = delete;

  void set_high() { gpio_set_level(pin_, 1); }
  void set_low() { gpio_set_level(pin_, 0); }
  void set(bool level) { gpio_set_level(pin_, level ? 1 : 0); }
  void toggle() { set(!get()); }

  [[nodiscard]] bool get() const { return gpio_get_level(pin_) != 0; }

  [[nodiscard]] gpio_num_t pin() const { return pin_; }

private:
  gpio_num_t pin_;
};

/// GPIO input pin (RAII)
class InputPin {
public:
  explicit InputPin(gpio_num_t pin, Pull pull = Pull::None) : pin_(pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pull == Pull::Up || pull == Pull::UpDown)
                          ? GPIO_PULLUP_ENABLE
                          : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == Pull::Down || pull == Pull::UpDown)
                            ? GPIO_PULLDOWN_ENABLE
                            : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    assert(err == ESP_OK && "Failed to configure GPIO");
  }

  ~InputPin() { gpio_reset_pin(pin_); }

  InputPin(const InputPin &) = delete;
  InputPin &operator=(const InputPin &) = delete;
  InputPin(InputPin &&) = delete;
  InputPin &operator=(InputPin &&) = delete;

  [[nodiscard]] bool read() const { return gpio_get_level(pin_) != 0; }
  [[nodiscard]] bool is_high() const { return read(); }
  [[nodiscard]] bool is_low() const { return !read(); }

  [[nodiscard]] gpio_num_t pin() const { return pin_; }

private:
  gpio_num_t pin_;
};

/// GPIO input with interrupt support
class InterruptPin {
public:
  using Callback = void (*)(void *arg);

  explicit InterruptPin(gpio_num_t pin, gpio_int_type_t trigger,
                        Callback callback, void *arg = nullptr,
                        Pull pull = Pull::None)
      : pin_(pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pull == Pull::Up || pull == Pull::UpDown)
                          ? GPIO_PULLUP_ENABLE
                          : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == Pull::Down || pull == Pull::UpDown)
                            ? GPIO_PULLDOWN_ENABLE
                            : GPIO_PULLDOWN_DISABLE,
        .intr_type = trigger,
    };
    esp_err_t err = gpio_config(&cfg);
    assert(err == ESP_OK && "Failed to configure GPIO");

    // Install ISR service if not already installed
    static bool isr_installed = false;
    if (!isr_installed) {
      gpio_install_isr_service(0);
      isr_installed = true;
    }

    err = gpio_isr_handler_add(pin_, callback, arg);
    assert(err == ESP_OK && "Failed to add ISR handler");
  }

  ~InterruptPin() {
    gpio_isr_handler_remove(pin_);
    gpio_reset_pin(pin_);
  }

  InterruptPin(const InterruptPin &) = delete;
  InterruptPin &operator=(const InterruptPin &) = delete;
  InterruptPin(InterruptPin &&) = delete;
  InterruptPin &operator=(InterruptPin &&) = delete;

  [[nodiscard]] bool read() const { return gpio_get_level(pin_) != 0; }

  void enable_interrupt() { gpio_intr_enable(pin_); }
  void disable_interrupt() { gpio_intr_disable(pin_); }

  [[nodiscard]] gpio_num_t pin() const { return pin_; }

private:
  gpio_num_t pin_;
};

} // namespace core
