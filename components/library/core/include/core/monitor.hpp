/**
 * @file monitor.hpp
 * @brief Base monitor class for event-driven observers
 */

#pragma once

#include "event_loop.hpp"

namespace core {

/// Base monitor class - subscribes to events and can be enabled/disabled
class Monitor {
public:
  virtual ~Monitor() = default;

  Monitor(const Monitor &) = delete;
  Monitor &operator=(const Monitor &) = delete;
  Monitor(Monitor &&) = delete;
  Monitor &operator=(Monitor &&) = delete;

  /// Check if monitor is subscribed
  [[nodiscard]] bool is_active() const { return subscription_.active(); }

  /// Enable/disable the monitor
  void set_enabled(bool enabled) { enabled_ = enabled; }
  [[nodiscard]] bool is_enabled() const { return enabled_; }

protected:
  Monitor() = default;

  /// Subscribe to an event (call from derived constructor)
  template <typename Evt>
  void subscribe(esp_event_base_t base, Evt event_id,
                 esp_event_handler_t handler) {
    subscription_ = events().subscribe(base, event_id, handler, this);
  }

  bool enabled_ = true;

private:
  EventSubscription subscription_;
};

} // namespace core
