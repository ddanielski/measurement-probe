/**
 * @file cloud_client.hpp
 * @brief Low-level cloud API client
 *
 * Handles HTTP communication with the backend. Does NOT know about:
 * - Sensor measurements (use TelemetryService)
 * - Command parsing (use CommandService)
 * - Protobuf serialization
 *
 * This class only handles: transport, auth injection, and raw HTTP.
 */

#pragma once

#include "config.hpp"
#include "device_auth.hpp"

#include <core/result.hpp>
#include <transport/http_transport.hpp>

#include <esp_log.h>

#include <span>
#include <string_view>
#include <vector>

namespace cloud {

/// Error codes for cloud operations
enum class CloudError : uint8_t {
  None = 0,
  NotInitialized,
  NotAuthenticated,
  DeviceRevoked,
  NetworkError,
  ServerError,
  ParseError,
  RateLimited,
};

/// Generic API response (owns body data)
struct ApiResponse {
  bool success{false};
  int status_code{0};
  CloudError error{CloudError::None};
  std::vector<uint8_t> body{}; // Owns response data

  /// Get body as string view
  [[nodiscard]] std::string_view body_str() const {
    return {reinterpret_cast<const char *>(body.data()), body.size()};
  }

  /// Check if body is empty
  [[nodiscard]] bool body_empty() const { return body.empty(); }
};

/// Low-level cloud API client
///
/// @thread_safety Thread-safe via HttpTransport's internal mutex.
class CloudClient {
public:
  CloudClient(DeviceAuthProvider &auth, const CloudConfig &config)
      : auth_(auth), config_(config) {}

  /// Initialize transport (call after WiFi connected)
  [[nodiscard]] core::Status init() {
    transport::HttpTransportConfig transport_config{
        .base_url = config_.base_url,
        .timeout = config_.timeout,
        .skip_cert_verify = config_.skip_cert_verify,
    };

    transport_.emplace(transport_config, &auth_);

    if (auto status = transport_->connect(); !status) {
      ESP_LOGE(TAG, "Transport connect failed");
      return status;
    }

    ESP_LOGI(TAG, "Cloud client initialized");
    return core::Ok();
  }

  /// POST raw bytes to an endpoint
  [[nodiscard]] ApiResponse post(std::string_view path,
                                 std::span<const uint8_t> body,
                                 transport::ContentType content_type) {
    return execute(transport::HttpMethod::Post, path, body, content_type);
  }

  /// POST with no body
  [[nodiscard]] ApiResponse post(std::string_view path) {
    return execute(transport::HttpMethod::Post, path, {},
                   transport::ContentType::Json);
  }

  /// GET from an endpoint
  [[nodiscard]] ApiResponse get(std::string_view path) {
    return execute(transport::HttpMethod::Get, path, {},
                   transport::ContentType::Json);
  }

  /// GET with query params
  [[nodiscard]] ApiResponse get(std::string_view path,
                                std::span<const transport::QueryParam> params) {
    return execute_with_params(transport::HttpMethod::Get, path, params);
  }

  /// PUT raw bytes to an endpoint
  [[nodiscard]] ApiResponse put(std::string_view path,
                                std::span<const uint8_t> body,
                                transport::ContentType content_type) {
    return execute(transport::HttpMethod::Put, path, body, content_type);
  }

  [[nodiscard]] bool is_connected() const noexcept {
    return transport_ && transport_->is_connected();
  }

  [[nodiscard]] bool is_revoked() const { return auth_.is_revoked(); }

  void disconnect() {
    if (transport_) {
      (void)transport_->disconnect();
    }
  }

private:
  static constexpr const char *TAG = "CloudClient";

  [[nodiscard]] ApiResponse execute(transport::HttpMethod method,
                                    std::string_view path,
                                    std::span<const uint8_t> body,
                                    transport::ContentType content_type) {
    if (!transport_) {
      return {.error = CloudError::NotInitialized};
    }

    if (auto err = ensure_auth(); err != CloudError::None) {
      return {.error = err};
    }

    transport::Request request{
        .method = method,
        .path = path,
        .body = body,
        .content_type = content_type,
    };

    return do_request(request);
  }

  [[nodiscard]] ApiResponse
  execute_with_params(transport::HttpMethod method, std::string_view path,
                      std::span<const transport::QueryParam> params) {
    if (!transport_) {
      return {.error = CloudError::NotInitialized};
    }

    if (auto err = ensure_auth(); err != CloudError::None) {
      return {.error = err};
    }

    transport::Request request{
        .method = method,
        .path = path,
        .query_params = params,
    };

    return do_request(request);
  }

  [[nodiscard]] CloudError ensure_auth() {
    if (auth_.is_revoked()) {
      return CloudError::DeviceRevoked;
    }

    if (auth_.needs_refresh()) {
      if (auto status = auth_.refresh(); !status) {
        return CloudError::NotAuthenticated;
      }
    }

    return CloudError::None;
  }

  [[nodiscard]] ApiResponse do_request(const transport::Request &request) {
    auto result = transport_->send(request);

    if (!result) {
      return {.error = CloudError::NetworkError};
    }

    auth_.handle_response_status(result->status_code());

    ApiResponse response{
        .success = result->is_success(),
        .status_code = result->status_code(),
        .body = {result->body().begin(), result->body().end()},
    };

    if (!response.success) {
      if (result->is_server_error()) {
        response.error = CloudError::ServerError;
      } else if (result->status_code() == status::TOO_MANY_REQUESTS) {
        response.error = CloudError::RateLimited;
      } else {
        response.error = CloudError::NetworkError;
      }
    }

    return response;
  }

  DeviceAuthProvider &auth_;
  CloudConfig config_;
  std::optional<transport::HttpTransport> transport_;
};

} // namespace cloud
