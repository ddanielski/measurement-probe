/**
 * @file command.hpp
 * @brief Command types and structures for cloud commands
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace cloud {

/// Known command types
enum class CommandType : uint8_t {
  Unknown = 0,
  Reboot,
  FactoryReset,
  // Add new command types here
};

/// Convert string to command type
[[nodiscard]] constexpr CommandType parse_command_type(std::string_view type) {
  if (type == "reboot") {
    return CommandType::Reboot;
  }
  if (type == "factory_reset") {
    return CommandType::FactoryReset;
  }
  return CommandType::Unknown;
}

/// Convert command type to string
[[nodiscard]] constexpr std::string_view
command_type_to_string(CommandType type) {
  switch (type) {
  case CommandType::Reboot:
    return "reboot";
  case CommandType::FactoryReset:
    return "factory_reset";
  default:
    return "unknown";
  }
}

/// Command ID size (UUID: 36 chars + null)
inline constexpr size_t COMMAND_ID_SIZE = 37;

/// Maximum payload size for commands
inline constexpr size_t COMMAND_PAYLOAD_SIZE = 256;

/// Maximum commands to process in one poll
inline constexpr size_t MAX_COMMANDS = 8;

/// Command received from backend
struct Command {
  std::array<char, COMMAND_ID_SIZE> id{};
  CommandType type{CommandType::Unknown};
  std::array<char, COMMAND_PAYLOAD_SIZE> payload{};
  int64_t expires_at{0};

  [[nodiscard]] std::string_view id_view() const { return {id.data()}; }

  [[nodiscard]] std::string_view payload_view() const {
    return {payload.data()};
  }

  [[nodiscard]] bool is_valid() const {
    return id[0] != '\0' && type != CommandType::Unknown;
  }

  /// Set ID from string_view
  void set_id(std::string_view str) {
    size_t len = std::min(str.size(), id.size() - 1);
    std::copy_n(str.data(), len, id.data());
    id.at(len) = '\0';
  }

  /// Set payload from string_view
  void set_payload(std::string_view str) {
    size_t len = std::min(str.size(), payload.size() - 1);
    std::copy_n(str.data(), len, payload.data());
    payload.at(len) = '\0';
  }
};

/// Fixed-capacity command buffer (no heap allocation)
class CommandBuffer {
public:
  using iterator = Command *;
  using const_iterator = const Command *;

  void clear() { count_ = 0; }

  [[nodiscard]] bool push(const Command &cmd) {
    if (count_ >= MAX_COMMANDS) {
      return false;
    }
    commands_.at(count_++) = cmd;
    return true;
  }

  [[nodiscard]] size_t size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0; }
  [[nodiscard]] bool full() const { return count_ >= MAX_COMMANDS; }

  [[nodiscard]] Command &operator[](size_t i) { return commands_.at(i); }
  [[nodiscard]] const Command &operator[](size_t i) const {
    return commands_.at(i);
  }

  [[nodiscard]] iterator begin() { return commands_.data(); }
  [[nodiscard]] iterator end() { return commands_.data() + count_; }
  [[nodiscard]] const_iterator begin() const { return commands_.data(); }
  [[nodiscard]] const_iterator end() const { return commands_.data() + count_; }

private:
  std::array<Command, MAX_COMMANDS> commands_{};
  size_t count_{0};
};

} // namespace cloud
