/**
 * @file http_transport.hpp
 * @brief HTTP/HTTPS transport implementation
 *
 * Implements ITransport using HTTP REST API calls.
 * Features:
 * - Keep-alive connections
 * - TLS support with certificate verification
 * - Authentication via IAuthProvider
 * - Path + query parameter URL building
 * - Content-type handling (JSON/Protobuf)
 */

#pragma once

#include "auth.hpp"
#include "transport.hpp"

#include <core/http_client.hpp>
#include <core/mutex.hpp>
#include <core/task.hpp>

#include <esp_log.h>

#include <atomic>
#include <cctype>
#include <memory>
#include <optional>
#include <queue>
#include <string>

namespace transport {

/// HTTP transport configuration
struct HttpTransportConfig {
  std::string_view base_url;
  std::chrono::milliseconds timeout{30000};
  std::chrono::seconds keep_alive{60};
  bool skip_cert_verify{false};
  std::string_view ca_cert{};     // PEM format, must outlive transport
  std::string_view client_cert{}; // PEM format, must outlive transport
  std::string_view client_key{};  // PEM format, must outlive transport

  std::string_view commands_path{"/commands"};

  uint32_t async_task_stack{4096};
  UBaseType_t async_task_priority{5};
};

/// HTTP transport implementation
///
/// Provides REST API transport using ESP-IDF's HTTP client.
/// Supports both synchronous and asynchronous operations.
///
/// @thread_safety Thread-safe for send operations (protected by mutex).
///                Async operations run on dedicated task.
class HttpTransport final : public ITransport {
public:
  /// Create HTTP transport
  /// @param config Transport configuration
  /// @param auth Optional authentication provider
  explicit HttpTransport(const HttpTransportConfig &config,
                         IAuthProvider *auth = nullptr)
      : auth_(auth), config_(config) {}

  ~HttpTransport() override { (void)disconnect(); }

  // Non-copyable
  HttpTransport(const HttpTransport &) = delete;
  HttpTransport &operator=(const HttpTransport &) = delete;

  // Non-movable (due to async task)
  HttpTransport(HttpTransport &&) = delete;
  HttpTransport &operator=(HttpTransport &&) = delete;

  // ITransport implementation

  [[nodiscard]] core::Status connect() override {
    core::LockGuard lock(mutex_);

    if (client_) {
      return core::Ok();
    }

    core::HttpClientConfig http_config{
        .base_url = config_.base_url,
        .timeout = config_.timeout,
        .keep_alive_idle = config_.keep_alive,
        .skip_cert_verify = config_.skip_cert_verify,
        .ca_cert = config_.ca_cert,
        .client_cert = config_.client_cert,
        .client_key = config_.client_key,
    };

    client_.emplace(http_config);

    if (!client_->valid()) {
      client_.reset();
      ESP_LOGE(TAG, "Failed to create HTTP client");
      return core::Err(ESP_FAIL);
    }

    connected_ = true;
    ESP_LOGI(TAG, "Connected to %.*s",
             static_cast<int>(config_.base_url.size()),
             config_.base_url.data());
    return core::Ok();
  }

  [[nodiscard]] core::Status disconnect() override {
    // Stop async task first
    stop_async_task();

    core::LockGuard lock(mutex_);

    if (client_) {
      client_.reset();
      connected_ = false;
      ESP_LOGI(TAG, "Disconnected");
    }
    return core::Ok();
  }

  [[nodiscard]] bool is_connected() const noexcept override {
    return connected_.load();
  }

  [[nodiscard]] core::Result<Response> send(const Request &request) override {
    core::LockGuard lock(mutex_);

    if (!client_ || !client_->valid()) {
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    // Apply authentication if available
    if (auth_ == nullptr) {
      ESP_LOGW(TAG, "No auth provider set");
    } else if (!auth_->has_credentials()) {
      ESP_LOGW(TAG, "Auth provider has no credentials");
    } else {
      auto auth_result = auth_->get_auth_header();
      if (auth_result) {
        ESP_LOGI(TAG, "Setting auth header: %zu bytes",
                 auth_result->value.size());
        auto status = client_->set_auth_header(auth_result->value);
        if (!status) {
          ESP_LOGW(TAG, "Failed to set auth header: %s",
                   esp_err_to_name(status.error()));
        }
      } else {
        ESP_LOGW(TAG, "get_auth_header failed: %s",
                 esp_err_to_name(auth_result.error()));
      }
    }

    // Build URL with query parameters
    std::string path = build_url(request.path, request.query_params);

    // Map content type and method
    auto http_content_type = map_content_type(request.content_type);
    auto http_method = map_method(request.method);

    // Perform request
    auto result =
        client_->perform(http_method, path, request.body, http_content_type);

    if (!result) {
      connected_ = false; // Connection might be broken
      return core::Err(result.error());
    }

    // Convert to transport Response
    return Response(result->body_span(),
                    static_cast<uint16_t>(result->status_code));
  }

  [[nodiscard]] core::Status send_async(const Request &request,
                                        OnComplete on_complete) override {
    // Ensure async task is running
    auto status = ensure_async_task();
    if (!status) {
      return status;
    }

    // Queue the request
    {
      core::LockGuard lock(async_mutex_);
      async_queue_.push(
          AsyncRequest{.method = request.method,
                       .path = std::string(request.path),
                       .body = std::vector<uint8_t>(request.body.begin(),
                                                    request.body.end()),
                       .content_type = request.content_type,
                       .query_params = copy_query_params(request.query_params),
                       .callback = std::move(on_complete)});
    }

    // Wake up the async task
    xTaskNotifyGive(async_task_handle_);

    return core::Ok();
  }

  [[nodiscard]] core::Result<Response>
  receive(std::chrono::milliseconds timeout) override {
    // For HTTP, "receive" means polling an endpoint for commands
    // This is typically GET /commands or similar

    core::LockGuard lock(mutex_);

    if (!client_ || !client_->valid()) {
      return core::Err(ESP_ERR_INVALID_STATE);
    }

    // Apply authentication
    if (auth_ != nullptr && auth_->has_credentials()) {
      auto auth_result = auth_->get_auth_header();
      if (auth_result) {
        [[maybe_unused]] auto _ = client_->set_auth_header(auth_result->value);
      }
    }

    // Poll commands endpoint
    auto result = client_->perform(core::HttpMethod::Get,
                                   std::string(config_.commands_path));

    if (!result) {
      if (result.error() == ESP_ERR_HTTP_FETCH_HEADER) {
        // Timeout or no data - not an error
        return core::Err(ESP_ERR_TIMEOUT);
      }
      connected_ = false;
      return core::Err(result.error());
    }

    if (result->status_code == 204 || result->length == 0) {
      // No content - no commands pending
      return core::Err(ESP_ERR_TIMEOUT);
    }

    return Response(result->body_span(),
                    static_cast<uint16_t>(result->status_code));
  }

  /// Set or replace authentication provider
  void set_auth_provider(IAuthProvider *auth) {
    core::LockGuard lock(mutex_);
    auth_ = auth;
  }

  /// Get current auth provider
  [[nodiscard]] IAuthProvider *auth_provider() const { return auth_; }

private:
  static constexpr const char *TAG = "HttpTransport";

  /// Async request structure
  struct AsyncRequest {
    HttpMethod method;
    std::string path;
    std::vector<uint8_t> body;
    ContentType content_type;
    std::vector<std::pair<std::string, std::string>> query_params;
    OnComplete callback;
  };

  /// URL encode a string (percent encoding)
  [[nodiscard]] static std::string url_encode(std::string_view str) {
    std::string result;
    result.reserve(str.size());

    for (char c : str) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
          c == '.' || c == '~') {
        result += c;
      } else {
        // Percent encode
        result += '%';
        constexpr char hex[] = "0123456789ABCDEF";
        result += hex[static_cast<unsigned char>(c) >> 4];
        result += hex[static_cast<unsigned char>(c) & 0x0F];
      }
    }
    return result;
  }

  /// Build URL with query parameters
  [[nodiscard]] static std::string
  build_url(std::string_view path, std::span<const QueryParam> params) {

    std::string url(path);

    if (params.empty()) {
      return url;
    }

    url += '?';
    bool first = true;
    for (const auto &param : params) {
      if (!first) {
        url += '&';
      }
      url += url_encode(param.key);
      url += '=';
      url += url_encode(param.value);
      first = false;
    }

    return url;
  }

  /// Copy query params to owned storage
  [[nodiscard]] static std::vector<std::pair<std::string, std::string>>
  copy_query_params(std::span<const QueryParam> params) {
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(params.size());
    for (const auto &p : params) {
      result.emplace_back(std::string(p.key), std::string(p.value));
    }
    return result;
  }

  /// Map transport ContentType to core::ContentType
  [[nodiscard]] static core::ContentType map_content_type(ContentType type) {
    switch (type) {
    case ContentType::Json:
      return core::ContentType::Json;
    case ContentType::Protobuf:
      return core::ContentType::Protobuf;
    case ContentType::OctetStream:
      return core::ContentType::OctetStream;
    }
    return core::ContentType::OctetStream;
  }

  /// Map transport HttpMethod to core::HttpMethod
  [[nodiscard]] static core::HttpMethod map_method(HttpMethod method) {
    switch (method) {
    case HttpMethod::Get:
      return core::HttpMethod::Get;
    case HttpMethod::Post:
      return core::HttpMethod::Post;
    case HttpMethod::Put:
      return core::HttpMethod::Put;
    case HttpMethod::Delete:
      return core::HttpMethod::Delete;
    case HttpMethod::Patch:
      return core::HttpMethod::Patch;
    }
    return core::HttpMethod::Post;
  }

  /// Ensure async task is running
  core::Status ensure_async_task() {
    if (async_task_handle_ != nullptr) {
      return core::Ok();
    }

    BaseType_t result =
        xTaskCreate(async_task_entry, "http_async", config_.async_task_stack,
                    this, config_.async_task_priority, &async_task_handle_);

    if (result != pdPASS) {
      ESP_LOGE(TAG, "Failed to create async task");
      return core::Err(ESP_ERR_NO_MEM);
    }

    return core::Ok();
  }

  /// Stop async task
  void stop_async_task() {
    if (async_task_handle_ != nullptr) {
      async_task_running_ = false;
      xTaskNotifyGive(async_task_handle_);

      // Wait for task to exit
      vTaskDelay(pdMS_TO_TICKS(100));

      // Force delete if still running (shouldn't happen)
      if (eTaskGetState(async_task_handle_) != eDeleted) {
        vTaskDelete(async_task_handle_);
      }
      async_task_handle_ = nullptr;
    }
  }

  /// Async task entry point
  static void async_task_entry(void *arg) {
    auto *self = static_cast<HttpTransport *>(arg);
    self->async_task_loop();
    vTaskDelete(nullptr);
  }

  /// Async task main loop
  void async_task_loop() {
    async_task_running_ = true;

    while (async_task_running_) {
      // Wait for notification
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

      // Process queued requests
      while (async_task_running_) {
        AsyncRequest req;

        {
          core::LockGuard lock(async_mutex_);
          if (async_queue_.empty()) {
            break;
          }
          req = std::move(async_queue_.front());
          async_queue_.pop();
        }

        // Build request and send
        std::vector<QueryParam> params;
        params.reserve(req.query_params.size());
        for (const auto &p : req.query_params) {
          params.push_back({p.first, p.second});
        }

        Request transport_req{.method = req.method,
                              .path = req.path,
                              .query_params = params,
                              .body = req.body,
                              .content_type = req.content_type};

        auto result = send(transport_req);

        // Invoke callback
        if (req.callback) {
          req.callback(std::move(result));
        }
      }
    }
  }

  IAuthProvider *auth_{nullptr};
  HttpTransportConfig config_;
  std::optional<core::DefaultHttpClient> client_;
  std::atomic<bool> connected_{false};
  core::Mutex mutex_;

  // Async support
  TaskHandle_t async_task_handle_{nullptr};
  std::atomic<bool> async_task_running_{false};
  std::queue<AsyncRequest> async_queue_;
  core::Mutex async_mutex_;
};

} // namespace transport
