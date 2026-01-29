/**
 * @file device_auth.hpp
 * @brief Device authentication provider
 *
 * Exchanges device credentials (device_id + secret) for JWT token.
 * Stores token in RTC memory to survive deep sleep.
 * Handles token refresh and re-authentication on 401.
 */

#pragma once

#include "config.hpp"
#include "credentials.hpp"
#include "endpoints.hpp"

#include <core/http_client.hpp>
#include <core/mutex.hpp>
#include <core/result.hpp>
#include <core/rtc_storage.hpp>
#include <transport/auth.hpp>

#include <esp_log.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace cloud {

/// Device authentication state
enum class AuthState : uint8_t {
  Unauthenticated,
  Authenticated,
  TokenExpired,
  Revoked,
};

/// Auth error codes
enum class AuthError : uint8_t {
  None = 0,
  NoCredentials,
  NetworkError,
  InvalidCredentials,
  DeviceRevoked,
  RateLimited,
  ParseError,
  ServerError,
};

/// Result of authentication operation
struct AuthResult {
  AuthState state{AuthState::Unauthenticated};
  AuthError error{AuthError::None};
};

/// Buffer sizes
namespace auth_buffers {
inline constexpr size_t JSON_BODY_SIZE = 256;
/// "Bearer " (7) + JWT token (~1500) + null
inline constexpr size_t AUTH_HEADER_SIZE = 2048;
} // namespace auth_buffers

/// Device authentication provider
///
/// Implements IAuthProvider for use with HttpTransport.
/// Zero heap allocation design.
///
/// @thread_safety Thread-safe. All methods are protected by internal mutex.
class DeviceAuthProvider final : public transport::IAuthProvider {
public:
  DeviceAuthProvider(const DeviceCredentials &creds,
                     core::RtcAuthToken *rtc_token, const CloudConfig &config)
      : creds_(creds), rtc_token_(rtc_token), config_(config) {}

  // IAuthProvider implementation

  [[nodiscard]] core::Result<transport::AuthHeader> get_auth_header() override {
    core::LockGuard lock(mutex_);

    if (state_ == AuthState::Revoked) {
      ESP_LOGW(TAG, "get_auth_header: device revoked");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (rtc_token_ == nullptr) {
      ESP_LOGW(TAG, "get_auth_header: rtc_token is null");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (!rtc_token_->is_valid()) {
      ESP_LOGW(TAG, "get_auth_header: rtc_token not valid (len=%u)",
               rtc_token_->token.length);
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    // Build "Bearer <token>" into fixed buffer
    auto token = rtc_token_->token.view();
    ESP_LOGD(TAG, "Building auth header with token len=%zu", token.size());

    int len =
        snprintf(auth_header_buffer_.data(), auth_header_buffer_.size(),
                 "Bearer %.*s", static_cast<int>(token.size()), token.data());

    if (len < 0 || static_cast<size_t>(len) >= auth_header_buffer_.size()) {
      return core::Err(ESP_ERR_INVALID_SIZE);
    }

    return transport::AuthHeader{
        .name = "Authorization",
        .value = std::string_view{auth_header_buffer_.data(),
                                  static_cast<size_t>(len)}};
  }

  [[nodiscard]] bool needs_refresh() const override {
    core::LockGuard lock(mutex_);

    if (state_ == AuthState::Revoked) {
      return false;
    }

    if (rtc_token_ == nullptr) {
      return true;
    }

    return rtc_token_->needs_refresh(config_.token_refresh_buffer);
  }

  [[nodiscard]] core::Status refresh() override {
    core::LockGuard lock(mutex_);

    if (state_ == AuthState::Revoked) {
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    // Try refresh if we have valid token
    if (rtc_token_ != nullptr && rtc_token_->is_valid()) {
      auto result = do_token_refresh();
      if (result) {
        return core::Ok();
      }
      ESP_LOGW(TAG, "Token refresh failed, re-authenticating");
    }

    return do_authenticate();
  }

  [[nodiscard]] bool has_credentials() const override {
    return creds_.is_valid();
  }

  [[nodiscard]] AuthState state() const {
    core::LockGuard lock(mutex_);
    return state_;
  }

  /// Perform initial authentication (call on boot)
  [[nodiscard]] AuthResult authenticate() {
    core::LockGuard lock(mutex_);

    // Check RTC token first
    if (rtc_token_ != nullptr && rtc_token_->is_valid()) {
      state_ = AuthState::Authenticated;
      ESP_LOGI(TAG, "Using cached RTC token");
      return {.state = AuthState::Authenticated};
    }

    auto status = do_authenticate();
    if (!status) {
      return {.state = state_, .error = last_error_};
    }

    return {.state = AuthState::Authenticated};
  }

  /// Handle HTTP response status
  void handle_response_status(int status_code) {
    core::LockGuard lock(mutex_);

    if (status_code == status::UNAUTHORIZED) {
      if (rtc_token_ != nullptr) {
        rtc_token_->clear();
      }
      state_ = AuthState::TokenExpired;
      ESP_LOGW(TAG, "Token expired (401)");
    } else if (status_code == status::FORBIDDEN) {
      if (rtc_token_ != nullptr) {
        rtc_token_->clear();
      }
      state_ = AuthState::Revoked;
      ESP_LOGE(TAG, "Device revoked (403)");
    }
  }

  [[nodiscard]] bool is_revoked() const {
    core::LockGuard lock(mutex_);
    return state_ == AuthState::Revoked;
  }

  void clear_token() {
    core::LockGuard lock(mutex_);
    if (rtc_token_ != nullptr) {
      rtc_token_->clear();
    }
    state_ = AuthState::Unauthenticated;
  }

private:
  static constexpr const char *TAG = "DeviceAuth";

  /// Build JSON auth body into buffer
  /// Returns length or -1 on error
  [[nodiscard]] int build_auth_json(std::span<char> buffer) const {
    auto device_id = creds_.device_id_view();
    auto secret = creds_.secret_view();

    return snprintf(buffer.data(), buffer.size(),
                    R"({"device_id":"%.*s","secret":"%.*s"})",
                    static_cast<int>(device_id.size()), device_id.data(),
                    static_cast<int>(secret.size()), secret.data());
  }

  /// Authenticate with backend using credentials
  [[nodiscard]] core::Status do_authenticate() {
    if (!creds_.is_valid()) {
      ESP_LOGE(TAG, "No credentials");
      last_error_ = AuthError::NoCredentials;
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    ESP_LOGI(TAG, "Authenticating device %s", creds_.device_id.data());

    core::HttpClientConfig http_config{
        .base_url = config_.base_url,
        .timeout = config_.timeout,
        .skip_cert_verify = config_.skip_cert_verify,
        .ca_cert = {},
        .client_cert = {},
        .client_key = {},
    };
    core::DefaultHttpClient client(http_config);

    if (!client.valid()) {
      ESP_LOGE(TAG, "Failed to create HTTP client");
      last_error_ = AuthError::NetworkError;
      return core::Err(ESP_FAIL);
    }

    // Build JSON body
    std::array<char, auth_buffers::JSON_BODY_SIZE> json_buffer{};
    int json_len = build_auth_json(json_buffer);

    if (json_len < 0 || static_cast<size_t>(json_len) >= json_buffer.size()) {
      last_error_ = AuthError::ParseError;
      return core::Err(ESP_ERR_INVALID_SIZE);
    }

    auto result = client.perform(
        core::HttpMethod::Post, endpoints::AUTH_DEVICE,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t *>(json_buffer.data()),
            static_cast<size_t>(json_len)),
        core::ContentType::Json);

    if (!result) {
      ESP_LOGE(TAG, "Auth request failed: %s", esp_err_to_name(result.error()));
      last_error_ = AuthError::NetworkError;
      return core::Err(result.error());
    }

    return handle_auth_response(result->status_code, result->body_view());
  }

  /// Refresh token using /auth/refresh endpoint
  [[nodiscard]] core::Status do_token_refresh() {
    if (rtc_token_ == nullptr || !rtc_token_->is_valid()) {
      ESP_LOGW(TAG, "No valid token for refresh");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    ESP_LOGI(TAG, "Refreshing token");

    core::HttpClientConfig http_config{
        .base_url = config_.base_url,
        .timeout = config_.timeout,
        .skip_cert_verify = config_.skip_cert_verify,
        .ca_cert = {},
        .client_cert = {},
        .client_key = {},
    };
    core::DefaultHttpClient client(http_config);

    if (!client.valid()) {
      return core::Err(ESP_FAIL);
    }

    // Build auth header for refresh request
    auto token = rtc_token_->token.view();
    std::array<char, auth_buffers::AUTH_HEADER_SIZE> header_buf{};
    int header_len =
        snprintf(header_buf.data(), header_buf.size(), "Bearer %.*s",
                 static_cast<int>(token.size()), token.data());

    if (header_len < 0 ||
        static_cast<size_t>(header_len) >= header_buf.size()) {
      return core::Err(ESP_ERR_INVALID_SIZE);
    }

    (void)client.set_auth_header(
        std::string_view{header_buf.data(), static_cast<size_t>(header_len)});

    // POST /auth/refresh with empty body
    auto result =
        client.perform(core::HttpMethod::Post, endpoints::AUTH_REFRESH,
                       std::span<const uint8_t>{}, core::ContentType::Json);

    if (!result) {
      ESP_LOGE(TAG, "Refresh request failed: %s",
               esp_err_to_name(result.error()));
      return core::Err(result.error());
    }

    if (result->status_code == status::UNAUTHORIZED) {
      // Token expired, need full re-auth
      ESP_LOGW(TAG, "Refresh returned 401, token expired");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    return handle_auth_response(result->status_code, result->body_view());
  }

  [[nodiscard]] core::Status handle_auth_response(int status_code,
                                                  std::string_view body) {
    if (status_code == status::FORBIDDEN) {
      state_ = AuthState::Revoked;
      last_error_ = AuthError::DeviceRevoked;
      ESP_LOGE(TAG, "Device revoked");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (status_code == status::UNAUTHORIZED) {
      last_error_ = AuthError::InvalidCredentials;
      ESP_LOGE(TAG, "Invalid credentials");
      return core::Err(ESP_ERR_INVALID_ARG);
    }

    if (status_code == status::TOO_MANY_REQUESTS) {
      last_error_ = AuthError::RateLimited;
      ESP_LOGW(TAG, "Rate limited");
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (status_code < 200 || status_code >= 300) {
      last_error_ = AuthError::ServerError;
      ESP_LOGE(TAG, "Auth failed: %d", status_code);
      if (!body.empty()) {
        ESP_LOGE(TAG, "Response: %.*s", static_cast<int>(body.size()),
                 body.data());
      }
      return core::Err(ESP_FAIL);
    }

    // Parse response
    auto parse_result = parse_auth_response(body);
    if (!parse_result) {
      last_error_ = AuthError::ParseError;
      return parse_result;
    }

    state_ = AuthState::Authenticated;
    last_error_ = AuthError::None;
    ESP_LOGI(TAG, "Authentication successful");
    return core::Ok();
  }

  /// Parse {"token": "...", "expires_in": N}
  [[nodiscard]] core::Status parse_auth_response(std::string_view json) {
    // Find "token": "..."
    auto token_pos = json.find("\"token\"");
    if (token_pos == std::string_view::npos) {
      ESP_LOGE(TAG, "No token in response");
      return core::Err(ESP_ERR_INVALID_RESPONSE);
    }

    auto colon = json.find(':', token_pos);
    if (colon == std::string_view::npos) {
      return core::Err(ESP_ERR_INVALID_RESPONSE);
    }

    auto quote_start = json.find('"', colon);
    if (quote_start == std::string_view::npos) {
      return core::Err(ESP_ERR_INVALID_RESPONSE);
    }

    auto quote_end = json.find('"', quote_start + 1);
    if (quote_end == std::string_view::npos) {
      return core::Err(ESP_ERR_INVALID_RESPONSE);
    }

    std::string_view token =
        json.substr(quote_start + 1, quote_end - quote_start - 1);

    // Find expires_in
    int64_t expires_in = 3600; // Default 1 hour
    auto expires_pos = json.find("\"expires_in\"");
    if (expires_pos != std::string_view::npos) {
      auto num_start = json.find(':', expires_pos);
      if (num_start != std::string_view::npos) {
        num_start++;
        while (num_start < json.size() &&
               (json[num_start] == ' ' || json[num_start] == '\t')) {
          num_start++;
        }
        expires_in = 0;
        while (num_start < json.size() && json[num_start] >= '0' &&
               json[num_start] <= '9') {
          expires_in = expires_in * 10 + (json[num_start] - '0');
          num_start++;
        }
      }
    }

    auto expires_at =
        std::chrono::system_clock::now() + std::chrono::seconds(expires_in);

    ESP_LOGI(TAG, "Parsed token len=%zu, expires_in=%lld", token.size(),
             expires_in);

    if (rtc_token_ != nullptr) {
      rtc_token_->set(token, expires_at);
      ESP_LOGI(TAG, "Token stored, valid=%d, rtc_len=%u",
               rtc_token_->is_valid(), rtc_token_->token.length);
    } else {
      ESP_LOGW(TAG, "rtc_token_ is null, cannot store token");
    }

    return core::Ok();
  }

  const DeviceCredentials &creds_;
  core::RtcAuthToken *rtc_token_;
  CloudConfig config_;

  mutable core::Mutex mutex_;
  AuthState state_{AuthState::Unauthenticated};
  AuthError last_error_{AuthError::None};

  // Fixed buffers - no heap
  std::array<char, auth_buffers::AUTH_HEADER_SIZE> auth_header_buffer_{};
};

} // namespace cloud
