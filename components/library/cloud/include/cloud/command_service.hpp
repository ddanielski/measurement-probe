/**
 * @file command_service.hpp
 * @brief Command polling and parsing service
 *
 * Handles fetching commands from backend and parsing JSON responses.
 * Separated from CloudClient for single responsibility.
 */

#pragma once

#include "cloud_client.hpp"
#include "command.hpp"
#include "endpoints.hpp"

#include <esp_log.h>

#include <array>
#include <string_view>

namespace cloud {

/// Buffer sizes for command service
namespace command_buffers {
/// Path buffer for /commands/{uuid}/ack
inline constexpr size_t ACK_PATH_SIZE = 64;
} // namespace command_buffers

/// Result of command poll
struct CommandsResult {
  bool success{false};
  CloudError error{CloudError::None};
};

/// Command polling service
class CommandService {
public:
  explicit CommandService(CloudClient &client) : client_(client) {}

  /// Poll for pending commands
  [[nodiscard]] CommandsResult poll(CommandBuffer &buffer) {
    buffer.clear();

    transport::QueryParam params[] = {{.key = "status", .value = "pending"}};
    auto response = client_.get(endpoints::COMMANDS, params);

    if (!response.success) {
      return {.error = response.error};
    }

    if (response.status_code == status::NO_CONTENT || response.body_empty()) {
      return {.success = true};
    }

    if (!parse_commands(response.body_str(), buffer)) {
      return {.error = CloudError::ParseError};
    }

    return {.success = true};
  }

  /// Acknowledge command execution
  [[nodiscard]] core::Status ack(std::string_view command_id) {
    std::array<char, command_buffers::ACK_PATH_SIZE> path{};
    int len = snprintf(path.data(), path.size(), "%.*s/%.*s/ack",
                       static_cast<int>(endpoints::COMMANDS.size()),
                       endpoints::COMMANDS.data(),
                       static_cast<int>(command_id.size()), command_id.data());

    if (len < 0 || static_cast<size_t>(len) >= path.size()) {
      return core::Err(ESP_ERR_INVALID_SIZE);
    }

    auto response =
        client_.post(std::string_view{path.data(), static_cast<size_t>(len)});

    if (!response.success) {
      return core::Err(ESP_FAIL);
    }

    ESP_LOGI(TAG, "Command %.*s acked", static_cast<int>(command_id.size()),
             command_id.data());
    return core::Ok();
  }

private:
  static constexpr const char *TAG = "CmdService";

  /// Parse commands JSON into buffer
  [[nodiscard]] static bool parse_commands(std::string_view json,
                                           CommandBuffer &buffer) {
    auto data_pos = json.find("\"data\"");
    if (data_pos == std::string_view::npos) {
      return false;
    }

    auto array_start = json.find('[', data_pos);
    if (array_start == std::string_view::npos) {
      return false;
    }

    size_t pos = array_start + 1;
    while (pos < json.size() && !buffer.full()) {
      auto obj_start = json.find('{', pos);
      if (obj_start == std::string_view::npos)
        break;

      // Find matching closing brace (handle nested objects)
      int depth = 1;
      size_t obj_end = obj_start + 1;
      while (obj_end < json.size() && depth > 0) {
        if (json[obj_end] == '{')
          ++depth;
        else if (json[obj_end] == '}')
          --depth;
        ++obj_end;
      }

      if (depth != 0)
        break;

      Command cmd;
      if (parse_command(json.substr(obj_start, obj_end - obj_start), cmd)) {
        (void)buffer.push(cmd);
      }

      pos = obj_end;
    }

    return true;
  }

  /// Parse single command object
  [[nodiscard]] static bool parse_command(std::string_view json, Command &cmd) {
    auto id = extract_field(json, "id");
    if (id.empty()) {
      return false;
    }

    cmd.set_id(id);
    cmd.type = parse_command_type(extract_field(json, "type"));

    // Extract payload object if present
    auto payload_pos = json.find("\"payload\"");
    if (payload_pos != std::string_view::npos) {
      auto colon = json.find(':', payload_pos);
      if (colon != std::string_view::npos) {
        size_t start = colon + 1;
        while (start < json.size() && json[start] == ' ')
          ++start;

        if (start < json.size() && json[start] == '{') {
          int depth = 1;
          size_t end = start + 1;
          while (end < json.size() && depth > 0) {
            if (json[end] == '{')
              ++depth;
            else if (json[end] == '}')
              --depth;
            ++end;
          }
          cmd.set_payload(json.substr(start, end - start));
        }
      }
    }

    return true;
  }

  /// Extract string field value
  [[nodiscard]] static std::string_view extract_field(std::string_view json,
                                                      std::string_view name) {
    // Search for "name":
    size_t pos = 0;
    while (pos < json.size()) {
      auto quote = json.find('"', pos);
      if (quote == std::string_view::npos) {
        break;
      }

      auto end_quote = json.find('"', quote + 1);
      if (end_quote == std::string_view::npos) {
        break;
      }

      auto field_name = json.substr(quote + 1, end_quote - quote - 1);
      if (field_name == name) {
        // Find colon and value
        auto colon = json.find(':', end_quote);
        if (colon == std::string_view::npos) {
          break;
        }

        auto val_quote = json.find('"', colon);
        if (val_quote == std::string_view::npos) {
          break;
        }

        auto val_end = json.find('"', val_quote + 1);
        if (val_end == std::string_view::npos) {
          break;
        }

        return json.substr(val_quote + 1, val_end - val_quote - 1);
      }

      pos = end_quote + 1;
    }

    return {};
  }

  CloudClient &client_;
};

} // namespace cloud
