/**
 * @file config.hpp
 * @brief Cloud service configuration
 *
 * Defines configuration for connecting to the backend API.
 * Designed to be environment-agnostic (dev/staging/prod).
 */

#pragma once

#include "endpoints.hpp"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace cloud {

/// Default configuration values
namespace defaults {
inline constexpr std::chrono::milliseconds REQUEST_TIMEOUT{30000};
inline constexpr std::chrono::seconds TOKEN_REFRESH_BUFFER{300}; // 5 minutes
inline constexpr size_t MAX_TELEMETRY_SIZE = 1024 * 1024;        // 1MB
} // namespace defaults

/// Cloud service configuration
struct CloudConfig {
  std::string_view base_url{endpoints::BASE_URL};
  std::chrono::milliseconds timeout{defaults::REQUEST_TIMEOUT};
  std::chrono::seconds token_refresh_buffer{defaults::TOKEN_REFRESH_BUFFER};
  size_t max_telemetry_size{defaults::MAX_TELEMETRY_SIZE};
  bool skip_cert_verify{false};
};

/// HTTP status codes
namespace status {
inline constexpr int OK = 200;
inline constexpr int CREATED = 201;
inline constexpr int NO_CONTENT = 204;
inline constexpr int BAD_REQUEST = 400;
inline constexpr int UNAUTHORIZED = 401;
inline constexpr int FORBIDDEN = 403;
inline constexpr int NOT_FOUND = 404;
inline constexpr int CONFLICT = 409;
inline constexpr int TOO_MANY_REQUESTS = 429;
inline constexpr int SERVER_ERROR = 500;
} // namespace status

/// Rate limit constants
namespace rate_limits {
inline constexpr int AUTH_REQUESTS_PER_MINUTE = 10;
} // namespace rate_limits

} // namespace cloud
