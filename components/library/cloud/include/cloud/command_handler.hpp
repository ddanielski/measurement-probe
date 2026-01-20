/**
 * @file command_handler.hpp
 * @brief Command dispatcher and handlers
 *
 * Receives commands from CloudClient and dispatches to appropriate handlers.
 * Emits events for system operations
 */

#pragma once

#include "command.hpp"
#include "command_service.hpp"
#include "events.hpp"

#include <core/event_loop.hpp>
#include <core/result.hpp>

#include <esp_log.h>

#include <array>
#include <functional>
#include <string_view>

namespace cloud {

/// Command execution result
enum class CommandResult : uint8_t {
  Success,
  Failed,
  Unknown,
  InvalidPayload,
};

/// Command handler function type
using CommandHandlerFn = std::function<CommandResult(std::string_view payload)>;

/// Maximum number of custom handlers
inline constexpr size_t MAX_CUSTOM_HANDLERS = 8;

/// Command dispatcher
///
/// @thread_safety NOT thread-safe. Use from single task only.
class CommandHandler {
public:
  CommandHandler() = default;

  /// Register a custom command handler for a specific type
  bool register_handler(CommandType type, CommandHandlerFn handler) {
    if (custom_handler_count_ >= MAX_CUSTOM_HANDLERS) {
      return false;
    }
    custom_handlers_.at(custom_handler_count_++) = {
        .type = type, .handler = std::move(handler)};
    return true;
  }

  /// Process a command
  [[nodiscard]] CommandResult process(const Command &cmd) {
    ESP_LOGI(TAG, "Processing command: type=%s, id=%s",
             command_type_to_string(cmd.type).data(), cmd.id.data());

    // Check custom handlers first
    for (size_t i = 0; i < custom_handler_count_; ++i) {
      if (custom_handlers_.at(i).type == cmd.type) {
        return custom_handlers_.at(i).handler(cmd.payload_view());
      }
    }

    // Built-in handlers
    switch (cmd.type) {
    case CommandType::Reboot:
      return handle_reboot();
    case CommandType::FactoryReset:
      return handle_factory_reset();
    default:
      ESP_LOGW(TAG, "Unknown command type");
      return CommandResult::Unknown;
    }
  }

  /// Process all commands from buffer
  size_t process_all(CommandService &service, CommandBuffer &buffer) {
    size_t success_count = 0;

    for (const auto &cmd : buffer) {
      auto result = process(cmd);

      // Always acknowledge (so server knows we received it)
      auto ack_status = service.ack(cmd.id_view());

      if (result == CommandResult::Success) {
        success_count++;
        ESP_LOGI(TAG, "Command %s executed", cmd.id.data());
      } else {
        ESP_LOGW(TAG, "Command %s failed: %d", cmd.id.data(),
                 static_cast<int>(result));
      }

      if (!ack_status) {
        ESP_LOGW(TAG, "Failed to ack command %s", cmd.id.data());
      }
    }

    return success_count;
  }

private:
  static constexpr const char *TAG = "CmdHandler";

  struct CustomHandler {
    CommandType type{CommandType::Unknown};
    CommandHandlerFn handler;
  };

  [[nodiscard]] CommandResult handle_reboot() {
    ESP_LOGI(TAG, "Reboot requested - emitting event");
    core::events().publish(CLOUD_EVENTS, CloudEvent::RebootRequested);
    return CommandResult::Success;
  }

  [[nodiscard]] CommandResult handle_factory_reset() {
    ESP_LOGW(TAG, "Factory reset requested - emitting event");
    core::events().publish(CLOUD_EVENTS, CloudEvent::FactoryResetRequested);
    return CommandResult::Success;
  }

  std::array<CustomHandler, MAX_CUSTOM_HANDLERS> custom_handlers_{};
  size_t custom_handler_count_{0};
};

} // namespace cloud
