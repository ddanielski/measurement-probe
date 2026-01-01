/**
 * @file event_loop.hpp
 * @brief RAII wrapper for ESP-IDF default event loop
 */

#pragma once

#include <esp_event.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace core {

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CORE_EVENT_DEFINE_BASE(name) ESP_EVENT_DEFINE_BASE(name)
#define CORE_EVENT_DECLARE_BASE(name) ESP_EVENT_DECLARE_BASE(name)
// NOLINTEND(cppcoreguidelines-macro-usage)

/// Valid event ID types (enum or integral)
template <typename T>
concept EventId = std::is_enum_v<T> || std::is_integral_v<T>;

/// RAII event handler (auto-unsubscribes)
class EventSubscription {
public:
  EventSubscription() = default;

  EventSubscription(esp_event_base_t base, int32_t event_id,
                    esp_event_handler_instance_t instance)
      : base_(base), id_(event_id), instance_(instance) {}

  ~EventSubscription() { unsubscribe(); }

  EventSubscription(const EventSubscription &) = delete;
  EventSubscription &operator=(const EventSubscription &) = delete;

  EventSubscription(EventSubscription &&other) noexcept
      : base_(other.base_), id_(other.id_), instance_(other.instance_) {
    other.instance_ = nullptr;
  }

  EventSubscription &operator=(EventSubscription &&other) noexcept {
    if (this != &other) {
      unsubscribe();
      base_ = other.base_;
      id_ = other.id_;
      instance_ = other.instance_;
      other.instance_ = nullptr;
    }
    return *this;
  }

  void unsubscribe() {
    if (instance_ != nullptr) {
      esp_event_handler_instance_unregister(base_, id_, instance_);
      instance_ = nullptr;
    }
  }

  [[nodiscard]] bool active() const { return instance_ != nullptr; }
  [[nodiscard]] explicit operator bool() const { return active(); }

private:
  esp_event_base_t base_ = nullptr;
  int32_t id_ = 0;
  esp_event_handler_instance_t instance_ = nullptr;
};

/// Event bus using ESP-IDF default event loop
class EventBus {
public:
  static esp_err_t initialize() {
    auto &bus = get();
    if (bus.ready_.exchange(true)) {
      return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
      return ESP_OK; // Already created
    }
    return err;
  }

  static EventBus &get() {
    static EventBus bus;
    return bus;
  }

  EventBus(const EventBus &) = delete;
  EventBus &operator=(const EventBus &) = delete;
  EventBus(EventBus &&) = delete;
  EventBus &operator=(EventBus &&) = delete;

  /// Subscribe to events
  template <EventId Evt>
  [[nodiscard]] EventSubscription subscribe(esp_event_base_t base, Evt event_id,
                                            esp_event_handler_t handler,
                                            void *arg = nullptr) {
    esp_event_handler_instance_t inst = nullptr;
    esp_err_t err = esp_event_handler_instance_register(
        base, static_cast<int32_t>(event_id), handler, arg, &inst);
    if (err != ESP_OK) {
      return {};
    }
    return {base, static_cast<int32_t>(event_id), inst};
  }

  /// Publish event (with optional data pointer)
  template <EventId Evt, typename DataPtr = std::nullptr_t>
    requires std::is_pointer_v<DataPtr> || std::is_null_pointer_v<DataPtr>
  esp_err_t publish(esp_event_base_t base, Evt event_id, DataPtr data = nullptr,
                    TickType_t timeout = portMAX_DELAY) {
    if (stopped_.load()) {
      return ESP_ERR_INVALID_STATE;
    }
    if constexpr (std::is_null_pointer_v<DataPtr>) {
      return esp_event_post(base, static_cast<int32_t>(event_id), nullptr, 0,
                            timeout);
    } else {
      using Value = std::remove_cv_t<std::remove_pointer_t<DataPtr>>;
      return esp_event_post(base, static_cast<int32_t>(event_id),
                            static_cast<const void *>(data), sizeof(Value),
                            timeout);
    }
  }

  /// Publish from ISR (with optional data pointer)
  template <EventId Evt, typename DataPtr = std::nullptr_t>
    requires std::is_pointer_v<DataPtr> || std::is_null_pointer_v<DataPtr>
  esp_err_t publish_isr(esp_event_base_t base, Evt event_id,
                        DataPtr data = nullptr, BaseType_t *woken = nullptr) {
    if (stopped_.load()) {
      return ESP_ERR_INVALID_STATE;
    }
    if constexpr (std::is_null_pointer_v<DataPtr>) {
      return esp_event_isr_post(base, static_cast<int32_t>(event_id), nullptr,
                                0, woken);
    } else {
      using Value = std::remove_cv_t<std::remove_pointer_t<DataPtr>>;
      return esp_event_isr_post(base, static_cast<int32_t>(event_id),
                                static_cast<const void *>(data), sizeof(Value),
                                woken);
    }
  }

  [[nodiscard]] bool is_ready() const { return ready_.load(); }

private:
  EventBus() = default;
  ~EventBus() { stopped_.store(true); }

  std::atomic<bool> ready_{false};
  std::atomic<bool> stopped_{false};
};

inline EventBus &events() { return EventBus::get(); }

} // namespace core
