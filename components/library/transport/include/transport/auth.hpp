/**
 * @file auth.hpp
 * @brief Authentication providers for transport layer
 *
 * Defines interfaces and implementations for various authentication
 * mechanisms used with cloud services.
 */

#pragma once

#include <core/mutex.hpp>
#include <core/result.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string_view>

namespace transport {

/// Authentication header (views into provider's internal buffer)
struct AuthHeader {
  const char *name{nullptr};
  std::string_view value;
};

/// Authentication provider interface
class IAuthProvider {
public:
  virtual ~IAuthProvider() = default;

  /// Get the authentication header for requests
  [[nodiscard]] virtual core::Result<AuthHeader> get_auth_header() = 0;

  /// Check if credentials need refresh
  [[nodiscard]] virtual bool needs_refresh() const = 0;

  /// Refresh credentials if needed
  [[nodiscard]] virtual core::Status refresh() = 0;

  /// Check if provider has valid credentials
  [[nodiscard]] virtual bool has_credentials() const = 0;
};

/// Buffer sizes for auth providers
namespace auth_buffers {
/// JWT tokens can be 1000-1500+ bytes
inline constexpr size_t TOKEN_SIZE = 2048;
inline constexpr size_t HEADER_SIZE = 2100; // "Bearer " + token
} // namespace auth_buffers

/// Default auth timing
namespace auth_defaults {
inline constexpr std::chrono::seconds EXPIRY_BUFFER{60};
} // namespace auth_defaults

/// Token with expiry information (for refresh callbacks)
struct TokenInfo {
  std::string_view token;
  std::chrono::system_clock::time_point expires_at;
};

/// JWT Bearer token authentication provider
///
/// Manages JWT tokens for REST API authentication.
/// Zero heap allocation - uses fixed internal buffers.
///
/// @thread_safety Thread-safe. All methods are protected by internal mutex.
class JwtAuthProvider final : public IAuthProvider {
public:
  using RefreshCallback = std::function<core::Result<TokenInfo>()>;

  JwtAuthProvider() = default;

  /// Create provider with initial token
  explicit JwtAuthProvider(
      std::string_view token,
      std::chrono::system_clock::time_point expires_at = {}) {
    set_token(token, expires_at);
  }

  /// Set the JWT token
  void set_token(std::string_view token,
                 std::chrono::system_clock::time_point expires_at = {}) {
    core::LockGuard lock(mutex_);

    size_t len = std::min(token.size(), token_buffer_.size() - 1);
    std::copy_n(token.data(), len, token_buffer_.data());
    token_buffer_.at(len) = '\0';
    token_len_ = len;
    expires_at_ = expires_at;
  }

  void set_refresh_callback(RefreshCallback callback) {
    core::LockGuard lock(mutex_);
    refresh_callback_ = std::move(callback);
  }

  void set_expiry_buffer(std::chrono::seconds buffer) {
    core::LockGuard lock(mutex_);
    expiry_buffer_ = buffer;
  }

  [[nodiscard]] core::Result<AuthHeader> get_auth_header() override {
    core::LockGuard lock(mutex_);

    if (token_len_ == 0) {
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (needs_refresh_unlocked() && refresh_callback_) {
      if (auto status = refresh_unlocked(); !status) {
        return core::Err(status.error());
      }
    }

    // Build "Bearer <token>" into header buffer
    int len =
        snprintf(header_buffer_.data(), header_buffer_.size(), "Bearer %.*s",
                 static_cast<int>(token_len_), token_buffer_.data());

    if (len < 0 || static_cast<size_t>(len) >= header_buffer_.size()) {
      return core::Err(ESP_ERR_INVALID_SIZE);
    }

    return AuthHeader{
        .name = "Authorization",
        .value = {header_buffer_.data(), static_cast<size_t>(len)}};
  }

  [[nodiscard]] bool needs_refresh() const override {
    core::LockGuard lock(mutex_);
    return needs_refresh_unlocked();
  }

  [[nodiscard]] core::Status refresh() override {
    core::LockGuard lock(mutex_);
    return refresh_unlocked();
  }

  [[nodiscard]] bool has_credentials() const override {
    core::LockGuard lock(mutex_);
    return token_len_ > 0;
  }

  [[nodiscard]] std::string_view token() const {
    core::LockGuard lock(mutex_);
    return {token_buffer_.data(), token_len_};
  }

  [[nodiscard]] std::chrono::system_clock::time_point expires_at() const {
    core::LockGuard lock(mutex_);
    return expires_at_;
  }

private:
  [[nodiscard]] bool needs_refresh_unlocked() const {
    if (expires_at_ == std::chrono::system_clock::time_point{}) {
      return false;
    }
    return std::chrono::system_clock::now() >= (expires_at_ - expiry_buffer_);
  }

  [[nodiscard]] core::Status refresh_unlocked() {
    if (!refresh_callback_) {
      return core::Err(ESP_ERR_NOT_SUPPORTED);
    }

    auto result = refresh_callback_();
    if (!result) {
      return core::Err(result.error());
    }

    // Copy token into buffer
    size_t len = std::min(result->token.size(), token_buffer_.size() - 1);
    std::copy_n(result->token.data(), len, token_buffer_.data());
    token_buffer_.at(len) = '\0';
    token_len_ = len;
    expires_at_ = result->expires_at;

    return core::Ok();
  }

  mutable core::Mutex mutex_;
  std::array<char, auth_buffers::TOKEN_SIZE> token_buffer_{};
  std::array<char, auth_buffers::HEADER_SIZE> header_buffer_{};
  size_t token_len_{0};
  std::chrono::system_clock::time_point expires_at_{};
  std::chrono::seconds expiry_buffer_{auth_defaults::EXPIRY_BUFFER};
  RefreshCallback refresh_callback_;
};

/// API key authentication provider
///
/// Simple key-based authentication via header.
/// Zero heap allocation.
class ApiKeyAuthProvider final : public IAuthProvider {
public:
  enum class Location : uint8_t {
    Header,     // X-API-Key header
    BearerToken // Authorization: Bearer <key>
  };

  ApiKeyAuthProvider() = default;

  explicit ApiKeyAuthProvider(std::string_view key,
                              const char *header_name = "X-API-Key",
                              Location location = Location::Header)
      : header_name_(header_name), location_(location) {
    set_key(key);
  }

  void set_key(std::string_view key) {
    size_t len = std::min(key.size(), key_buffer_.size() - 1);
    std::copy_n(key.data(), len, key_buffer_.data());
    key_buffer_.at(len) = '\0';
    key_len_ = len;
  }

  [[nodiscard]] core::Result<AuthHeader> get_auth_header() override {
    if (key_len_ == 0) {
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    if (location_ == Location::BearerToken) {
      int len =
          snprintf(header_buffer_.data(), header_buffer_.size(), "Bearer %.*s",
                   static_cast<int>(key_len_), key_buffer_.data());

      if (len < 0 || static_cast<size_t>(len) >= header_buffer_.size()) {
        return core::Err(ESP_ERR_INVALID_SIZE);
      }

      return AuthHeader{
          .name = "Authorization",
          .value = {header_buffer_.data(), static_cast<size_t>(len)}};
    }

    return AuthHeader{.name = header_name_,
                      .value = {key_buffer_.data(), key_len_}};
  }

  [[nodiscard]] bool needs_refresh() const override { return false; }

  [[nodiscard]] core::Status refresh() override {
    return core::Err(ESP_ERR_NOT_SUPPORTED);
  }

  [[nodiscard]] bool has_credentials() const override { return key_len_ > 0; }

private:
  std::array<char, auth_buffers::TOKEN_SIZE> key_buffer_{};
  std::array<char, auth_buffers::HEADER_SIZE> header_buffer_{};
  size_t key_len_{0};
  const char *header_name_{"X-API-Key"};
  Location location_{Location::Header};
};

} // namespace transport
