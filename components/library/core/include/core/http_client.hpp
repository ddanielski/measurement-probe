/**
 * @file http_client.hpp
 * @brief RAII wrapper for esp_http_client
 *
 * Provides a modern C++ interface over ESP-IDF's HTTP client with:
 * - Zero heap allocation (template-sized buffers)
 * - Automatic resource cleanup
 * - Keep-alive connection support
 * - TLS configuration
 */

#pragma once

#include "result.hpp"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

namespace core {

/// HTTP methods
enum class HttpMethod : uint8_t {
  Get = HTTP_METHOD_GET,
  Post = HTTP_METHOD_POST,
  Put = HTTP_METHOD_PUT,
  Delete = HTTP_METHOD_DELETE,
  Patch = HTTP_METHOD_PATCH,
  Head = HTTP_METHOD_HEAD
};

/// Content types for HTTP requests
enum class ContentType : uint8_t {
  Json,
  Protobuf,
  OctetStream,
  FormUrlEnc,
  TextPlain
};

/// Convert ContentType to MIME string
[[nodiscard]] constexpr const char *
content_type_str(ContentType type) noexcept {
  switch (type) {
  case ContentType::Json:
    return "application/json";
  case ContentType::Protobuf:
    return "application/x-protobuf";
  case ContentType::OctetStream:
    return "application/octet-stream";
  case ContentType::FormUrlEnc:
    return "application/x-www-form-urlencoded";
  case ContentType::TextPlain:
    return "text/plain";
  }
  return "application/octet-stream";
}

/// Default configuration values
namespace http_defaults {
inline constexpr std::chrono::milliseconds TIMEOUT{30000};
inline constexpr std::chrono::seconds KEEP_ALIVE_IDLE{60};
inline constexpr std::chrono::seconds KEEP_ALIVE_INTERVAL{15};
inline constexpr uint8_t KEEP_ALIVE_COUNT = 3;
/// Buffer size for HTTP receive/transmit (must fit JWT auth header ~1500 bytes)
inline constexpr int HTTP_BUFFER_SIZE = 4096;
} // namespace http_defaults

/// HTTP client configuration
/// Note: All string_views must point to data with lifetime >= HttpClient
struct HttpClientConfig {
  std::string_view base_url;
  std::chrono::milliseconds timeout{http_defaults::TIMEOUT};
  std::chrono::seconds keep_alive_idle{http_defaults::KEEP_ALIVE_IDLE};
  std::chrono::seconds keep_alive_interval{http_defaults::KEEP_ALIVE_INTERVAL};
  uint8_t keep_alive_count{http_defaults::KEEP_ALIVE_COUNT};
  bool skip_cert_verify{false};
  std::string_view ca_cert;     // PEM format, must outlive client
  std::string_view client_cert; // PEM format, must outlive client
  std::string_view client_key;  // PEM format, must outlive client
  int buffer_size{http_defaults::HTTP_BUFFER_SIZE};
  int buffer_size_tx{http_defaults::HTTP_BUFFER_SIZE};
};

/// HTTP response view (points into client's internal buffer)
struct HttpResponse {
  const uint8_t *data{nullptr};
  size_t length{0};
  int status_code{0};
  size_t content_length{0};

  [[nodiscard]] bool is_success() const {
    return status_code >= 200 && status_code < 300;
  }

  [[nodiscard]] bool is_redirect() const {
    return status_code >= 300 && status_code < 400;
  }

  [[nodiscard]] bool is_client_error() const {
    return status_code >= 400 && status_code < 500;
  }

  [[nodiscard]] bool is_server_error() const { return status_code >= 500; }

  [[nodiscard]] std::string_view body_view() const {
    return {reinterpret_cast<const char *>(data), length};
  }

  [[nodiscard]] std::span<const uint8_t> body_span() const {
    return {data, length};
  }

  [[nodiscard]] bool empty() const { return length == 0; }
};

/// Default buffer sizes
namespace http_buffers {
inline constexpr size_t RESPONSE = 4096;
inline constexpr size_t URL = 256;
/// "Bearer " + JWT (~1500 bytes)
inline constexpr size_t AUTH = 2048;
} // namespace http_buffers

/// RAII wrapper for esp_http_client
///
/// @tparam ResponseSize Response buffer size (compile-time)
/// @tparam UrlSize URL buffer size for path building
/// @tparam AuthSize Auth header buffer size
///
/// @thread_safety NOT thread-safe. Use one instance per task or protect
///                with external mutex.
template <size_t ResponseSize = http_buffers::RESPONSE,
          size_t UrlSize = http_buffers::URL,
          size_t AuthSize = http_buffers::AUTH>
class HttpClient {
public:
  explicit HttpClient(const HttpClientConfig &config) { init(config); }

  ~HttpClient() {
    if (handle_ != nullptr) {
      esp_http_client_cleanup(handle_);
    }
  }

  // Non-copyable
  HttpClient(const HttpClient &) = delete;
  HttpClient &operator=(const HttpClient &) = delete;

  // Movable
  HttpClient(HttpClient &&other) noexcept
      : handle_(other.handle_), base_url_(other.base_url_),
        response_buffer_(other.response_buffer_),
        response_len_(other.response_len_) {
    other.handle_ = nullptr;
  }

  HttpClient &operator=(HttpClient &&other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        esp_http_client_cleanup(handle_);
      }
      handle_ = other.handle_;
      base_url_ = other.base_url_;
      response_buffer_ = other.response_buffer_;
      response_len_ = other.response_len_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

  /// Perform HTTP request
  [[nodiscard]] Result<HttpResponse>
  perform(HttpMethod method, std::string_view path,
          std::span<const uint8_t> body = {},
          ContentType content_type = ContentType::Json) {

    if (handle_ == nullptr) {
      return Err(ESP_ERR_INVALID_STATE);
    }

    // Reset response
    response_len_ = 0;

    // Build URL into fixed buffer
    if (!build_url(path)) {
      return Err(ESP_ERR_INVALID_SIZE);
    }

    esp_err_t err = esp_http_client_set_url(handle_, url_buffer_.data());
    if (err != ESP_OK)
      return Err(err);

    err = esp_http_client_set_method(
        handle_, static_cast<esp_http_client_method_t>(method));
    if (err != ESP_OK)
      return Err(err);

    err = esp_http_client_set_header(handle_, "Content-Type",
                                     content_type_str(content_type));
    if (err != ESP_OK) {
      return Err(err);
    }

    if (!body.empty()) {
      err = esp_http_client_set_post_field(
          handle_, reinterpret_cast<const char *>(body.data()),
          static_cast<int>(body.size()));
      if (err != ESP_OK) {
        return Err(err);
      }
    }

    // Re-apply auth header right before perform (in case set_url cleared it)
    if (auth_buffer_[0] != '\0') {
      size_t auth_len = strlen(auth_buffer_.data());
      ESP_LOGI(TAG, "Re-applying auth header: %zu bytes", auth_len);
      err = esp_http_client_set_header(handle_, "Authorization",
                                       auth_buffer_.data());
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to re-apply auth header: %s",
                 esp_err_to_name(err));
      }
    } else {
      ESP_LOGW(TAG, "No auth header in buffer to apply");
    }

    err = esp_http_client_perform(handle_);

    // Log response info regardless of error
    int status = esp_http_client_get_status_code(handle_);
    int64_t content_len = esp_http_client_get_content_length(handle_);
    ESP_LOGI(TAG, "HTTP response: status=%d, content_len=%lld, body_len=%zu",
             status, content_len, response_len_);

    if (err != ESP_OK) {
      ESP_LOGE(TAG, "HTTP request failed: %s (status was %d)",
               esp_err_to_name(err), status);
      // Log response body if available (might contain error message)
      if (response_len_ > 0) {
        ESP_LOGI(TAG, "Response body: %.*s",
                 static_cast<int>(std::min(response_len_, size_t{200})),
                 reinterpret_cast<const char *>(response_buffer_.data()));
      }
      return Err(err);
    }

    HttpResponse response{
        .data = response_buffer_.data(),
        .length = response_len_,
        .status_code = status,
        .content_length = static_cast<size_t>(content_len),
    };

    return response;
  }

  /// Set authorization header
  Status set_auth_header(std::string_view auth_value) {
    if (handle_ == nullptr)
      return Err(ESP_ERR_INVALID_STATE);

    if (auth_value.size() >= auth_buffer_.size())
      return Err(ESP_ERR_INVALID_SIZE);

    std::copy_n(auth_value.data(), auth_value.size(), auth_buffer_.data());
    auth_buffer_[auth_value.size()] = '\0';

    esp_err_t err = esp_http_client_set_header(handle_, "Authorization",
                                               auth_buffer_.data());
    ESP_LOGI(TAG, "esp_http_client_set_header(Authorization, %zu bytes): %s",
             auth_value.size(), esp_err_to_name(err));
    return err == ESP_OK ? Ok() : Err(err);
  }

  /// Set custom header (key and value must outlive the request)
  Status set_header(const char *key, const char *value) {
    if (handle_ == nullptr)
      return Err(ESP_ERR_INVALID_STATE);

    esp_err_t err = esp_http_client_set_header(handle_, key, value);
    return err == ESP_OK ? Ok() : Err(err);
  }

  /// Delete header
  Status delete_header(const char *key) {
    if (handle_ == nullptr)
      return Err(ESP_ERR_INVALID_STATE);

    esp_err_t err = esp_http_client_delete_header(handle_, key);
    return err == ESP_OK ? Ok() : Err(err);
  }

  [[nodiscard]] static constexpr size_t buffer_capacity() {
    return ResponseSize;
  }

  [[nodiscard]] esp_http_client_handle_t native_handle() const {
    return handle_;
  }

private:
  static constexpr const char *TAG = "HttpClient";

  void init(const HttpClientConfig &config) {
    // Enable debug logging for HTTP layer
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_DEBUG);

    base_url_ = config.base_url;

    esp_http_client_config_t esp_config{};

    // Build initial URL (base_url only)
    (void)build_url("");
    esp_config.url = url_buffer_.data();

    esp_config.timeout_ms = static_cast<int>(config.timeout.count());
    esp_config.buffer_size = config.buffer_size;
    esp_config.buffer_size_tx = config.buffer_size_tx;
    esp_config.keep_alive_enable = true;
    esp_config.keep_alive_idle =
        static_cast<int>(config.keep_alive_idle.count());
    esp_config.keep_alive_interval =
        static_cast<int>(config.keep_alive_interval.count());
    esp_config.keep_alive_count = config.keep_alive_count;

    // TLS configuration
    if (config.skip_cert_verify) {
      esp_config.skip_cert_common_name_check = true;
      esp_config.crt_bundle_attach = nullptr;
    } else if (!config.ca_cert.empty()) {
      esp_config.cert_pem = config.ca_cert.data();
    } else {
      esp_config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    // mTLS
    if (!config.client_cert.empty() && !config.client_key.empty()) {
      esp_config.client_cert_pem = config.client_cert.data();
      esp_config.client_key_pem = config.client_key.data();
    }

    esp_config.event_handler = http_event_handler;
    esp_config.user_data = this;

    // Disable automatic Basic/Digest auth - we handle Bearer auth manually
    esp_config.auth_type = HTTP_AUTH_TYPE_NONE;

    handle_ = esp_http_client_init(&esp_config);
  }

  [[nodiscard]] bool build_url(std::string_view path) {
    size_t total = base_url_.size() + path.size();
    if (total >= url_buffer_.size())
      return false;

    auto it =
        std::copy_n(base_url_.data(), base_url_.size(), url_buffer_.data());
    it = std::copy_n(path.data(), path.size(), it);
    *it = '\0';
    return true;
  }

  static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    auto *self = static_cast<HttpClient *>(evt->user_data);
    if (self == nullptr)
      return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
      size_t space = ResponseSize - self->response_len_;
      size_t copy_len = std::min(static_cast<size_t>(evt->data_len), space);

      if (copy_len < static_cast<size_t>(evt->data_len)) {
        ESP_LOGW(TAG, "Response truncated: buffer full");
      }

      std::memcpy(self->response_buffer_.data() + self->response_len_,
                  evt->data, copy_len);
      self->response_len_ += copy_len;
    }

    return ESP_OK;
  }

  esp_http_client_handle_t handle_{nullptr};
  std::string_view base_url_;

  std::array<uint8_t, ResponseSize> response_buffer_{};
  std::array<char, UrlSize> url_buffer_{};
  std::array<char, AuthSize> auth_buffer_{};
  size_t response_len_{0};
};

/// Type alias for common configuration
using DefaultHttpClient = HttpClient<>;

} // namespace core
