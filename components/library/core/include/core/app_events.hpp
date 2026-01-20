/**
 * @file app_events.hpp
 * @brief Application lifecycle events
 *
 * Events for coordinating application startup, shutdown, and error handling.
 * Application emits these; subsystems can subscribe to react appropriately.
 */

#pragma once

#include "event_loop.hpp"

#include <cstdint>

namespace core {

/// Application events base
CORE_EVENT_DECLARE_BASE(APP_EVENTS);

/// Application lifecycle events
enum class AppEvent : int32_t {
  StartupComplete,
  ShutdownRequested,
  EnteringLowPower,
  WokeFromLowPower,
  ErrorRecovery,
  ErrorRecovered,
};

/// Event data for error events
struct ErrorEventData {
  const char *component;
  int32_t error_code;
};

} // namespace core
