/**
 * @file transport.hpp
 * @brief Transport abstraction layer for cloud communication
 *
 * Defines the interface for sending/receiving data to/from cloud services.
 * Implementations include HTTP (REST API) and potentially MQTT in the future.
 */

#pragma once

#include <core/result.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace transport {

/// Content types for payload serialization
enum class ContentType : uint8_t {
  Json,       // application/json - for configuration, commands
  Protobuf,   // application/x-protobuf - for telemetry data
  OctetStream // application/octet-stream - for raw binary
};

/// HTTP methods
enum class HttpMethod : uint8_t { Get, Post, Put, Delete, Patch };

/// Query parameter for URL construction
struct QueryParam {
  std::string_view key;
  std::string_view value;
};

/// HTTP-like request structure
/// @note Uses non-owning views - caller must ensure data lifetime
struct Request {
  HttpMethod method{HttpMethod::Post}; // HTTP method
  std::string_view path;               // URL path (e.g., "/api/v1/telemetry")
  std::span<const QueryParam> query_params{}; // Optional query parameters
  std::span<const uint8_t> body{};            // Request body (protobuf or JSON)
  ContentType content_type{ContentType::Protobuf};
};

/// Response from transport
/// @note Owns the body data (stored in pre-allocated buffer)
class Response {
public:
  Response() = default;

  Response(std::vector<uint8_t> body_data, uint16_t status)
      : body_(std::move(body_data)), status_code_(status) {}

  Response(std::span<const uint8_t> body_view, uint16_t status)
      : body_(body_view.begin(), body_view.end()), status_code_(status) {}

  /// Get response body as span
  [[nodiscard]] std::span<const uint8_t> body() const {
    return {body_.data(), body_.size()};
  }

  /// Get response body as string view
  [[nodiscard]] std::string_view body_str() const {
    return {reinterpret_cast<const char *>(body_.data()), body_.size()};
  }

  /// Get HTTP status code
  [[nodiscard]] uint16_t status_code() const { return status_code_; }

  /// Check if request was successful (2xx)
  [[nodiscard]] bool is_success() const {
    return status_code_ >= 200 && status_code_ < 300;
  }

  /// Check if response indicates client error (4xx)
  [[nodiscard]] bool is_client_error() const {
    return status_code_ >= 400 && status_code_ < 500;
  }

  /// Check if response indicates server error (5xx)
  [[nodiscard]] bool is_server_error() const { return status_code_ >= 500; }

  /// Check if response body is empty
  [[nodiscard]] bool empty() const { return body_.empty(); }

private:
  std::vector<uint8_t> body_;
  uint16_t status_code_{0};
};

/// Execution policy for transport operations
enum class ExecutionPolicy : uint8_t {
  Sync, // Block caller's task until complete
  Async // Post to transport task, return immediately
};

/// Callback for async operations
using OnComplete = std::function<void(core::Result<Response>)>;

/// Transport interface for cloud communication
///
/// Abstract interface for sending/receiving data. Implementations handle
/// the actual protocol (HTTP, MQTT, etc.) and connection management.
class ITransport {
public:
  virtual ~ITransport() = default;

  /// Connect to the remote endpoint
  /// @return Status indicating success or connection error
  [[nodiscard]] virtual core::Status connect() = 0;

  /// Disconnect from the remote endpoint
  /// @return Status indicating success or error
  [[nodiscard]] virtual core::Status disconnect() = 0;

  /// Check if currently connected
  [[nodiscard]] virtual bool is_connected() const noexcept = 0;

  /// Send a request and receive response (synchronous)
  /// @param request The request to send
  /// @return Response or error
  [[nodiscard]] virtual core::Result<Response> send(const Request &request) = 0;

  /// Send a request asynchronously
  /// @param request The request to send
  /// @param on_complete Callback invoked when complete
  /// @return Status (ESP_OK if request was queued)
  [[nodiscard]] virtual core::Status send_async(const Request &request,
                                                OnComplete on_complete) = 0;

  /// Poll for incoming commands/messages (synchronous)
  /// @param timeout Maximum time to wait for data
  /// @return Response with command data, or error (ESP_ERR_TIMEOUT if none)
  [[nodiscard]] virtual core::Result<Response>
  receive(std::chrono::milliseconds timeout) = 0;
};

/// Transport configuration base
struct TransportConfig {
  std::string_view base_url;                // Base URL or endpoint
  std::chrono::milliseconds timeout{30000}; // Operation timeout
  std::chrono::seconds keep_alive{60};      // Connection keep-alive
};

} // namespace transport
