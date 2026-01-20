/**
 * @file events.hpp
 * @brief Cloud event definitions
 *
 * Events emitted by cloud services for the application to handle.
 * Using ESP-IDF event loop for decoupling.
 */

#pragma once

#include <core/event_loop.hpp>

#include <cstdint>

namespace cloud {

/// Cloud event base
CORE_EVENT_DECLARE_BASE(CLOUD_EVENTS);

/// Cloud event types
enum class CloudEvent : int32_t {
  Authenticated,
  AuthFailed,
  TokenRefreshed,
  Revoked,
  RebootRequested,
  FactoryResetRequested,
  TelemetrySent,
  TelemetryFailed,
  CommandReceived,
  CommandProcessed,
};

/// Event data for command events
struct CommandEventData {
  const char *command_id;
  int32_t command_type;
};

} // namespace cloud
