/**
 * @file retry.hpp
 * @brief Retry decorator for transport layer
 *
 * Provides automatic retry with exponential backoff for transient failures.
 * Wraps any ITransport implementation.
 */

#pragma once

#include "transport.hpp"

#include <core/task.hpp>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace transport {

/// Retry policy configuration
struct RetryPolicy {
  uint8_t max_retries{3};                        // Maximum retry attempts
  std::chrono::milliseconds initial_delay{1000}; // First retry delay
  std::chrono::milliseconds max_delay{30000}; // Maximum delay between retries
  float backoff_multiplier{2.0F};             // Delay multiplier per retry
  bool retry_on_timeout{true};                // Retry on timeout errors
  bool retry_on_server_error{true};           // Retry on 5xx responses
  bool retry_on_connection_error{true};       // Retry on connection failures
};

/// Retry transport decorator
///
/// Wraps another transport and automatically retries failed requests
/// using exponential backoff. Configurable via RetryPolicy.
///
/// @tparam BaseTransport The underlying transport type
template <typename BaseTransport>
class RetryTransport final : public ITransport {
public:
  /// Create retry transport wrapping base transport
  /// @param base The underlying transport (takes ownership)
  /// @param policy Retry policy configuration
  explicit RetryTransport(BaseTransport base, const RetryPolicy &policy = {})
      : base_(std::move(base)), policy_(policy) {}

  // Non-copyable
  RetryTransport(const RetryTransport &) = delete;
  RetryTransport &operator=(const RetryTransport &) = delete;

  // Movable
  RetryTransport(RetryTransport &&) = default;
  RetryTransport &operator=(RetryTransport &&) = default;

  // ITransport implementation

  [[nodiscard]] core::Status connect() override {
    return execute_with_retry([this]() { return base_.connect(); });
  }

  [[nodiscard]] core::Status disconnect() override {
    // Don't retry disconnect - if it fails, it fails
    return base_.disconnect();
  }

  [[nodiscard]] bool is_connected() const noexcept override {
    return base_.is_connected();
  }

  [[nodiscard]] core::Result<Response> send(const Request &request) override {
    uint8_t attempts = 0;
    auto delay = policy_.initial_delay;

    while (true) {
      auto result = base_.send(request);

      if (result) {
        // Check for server error responses
        if (result->is_server_error() && policy_.retry_on_server_error) {
          if (should_retry(attempts)) {
            wait_and_backoff(attempts, delay);
            continue;
          }
        }
        return result;
      }

      // Check if error is retryable
      if (!is_retryable_error(result.error())) {
        return result;
      }

      if (!should_retry(attempts)) {
        return result;
      }

      wait_and_backoff(attempts, delay);
    }
  }

  [[nodiscard]] core::Status send_async(const Request &request,
                                        OnComplete on_complete) override {
    // Create shared retry state with owned copies of request data
    auto state = std::make_shared<AsyncRetryState>(
        OwnedRequest::from(request), std::move(on_complete), policy_, this);

    return start_async_attempt(std::move(state));
  }

  [[nodiscard]] core::Result<Response>
  receive(std::chrono::milliseconds timeout) override {
    // Don't retry receive - caller controls polling
    return base_.receive(timeout);
  }

  /// Get reference to underlying transport
  [[nodiscard]] BaseTransport &base() { return base_; }
  [[nodiscard]] const BaseTransport &base() const { return base_; }

  /// Get current retry policy
  [[nodiscard]] const RetryPolicy &policy() const { return policy_; }

  /// Update retry policy
  void set_policy(const RetryPolicy &policy) { policy_ = policy; }

private:
  static constexpr const char *TAG = "RetryTransport";

  /// Check if error is retryable
  [[nodiscard]] bool is_retryable_error(esp_err_t err) const {
    switch (err) {
    case ESP_ERR_TIMEOUT:
      return policy_.retry_on_timeout;

    case ESP_ERR_INVALID_STATE:
    case ESP_FAIL:
    case ESP_ERR_NO_MEM:
      return policy_.retry_on_connection_error;

    default:
      // ESP HTTP client errors that indicate connection issues
      if (err >= 0x7000 && err <= 0x70FF) { // ESP_HTTP_CLIENT_ERR_BASE range
        return policy_.retry_on_connection_error;
      }
      return false;
    }
  }

  /// Check if we should retry (haven't exceeded max attempts)
  [[nodiscard]] bool should_retry(uint8_t attempts) const {
    return attempts < policy_.max_retries;
  }

  /// Wait for backoff delay and update for next attempt
  void wait_and_backoff(uint8_t &attempts, std::chrono::milliseconds &delay) {
    ESP_LOGW(TAG, "Retry %d/%d after %lldms", attempts + 1, policy_.max_retries,
             delay.count());

    core::Task::delay(delay);

    ++attempts;
    delay = std::chrono::milliseconds(std::min(
        static_cast<int64_t>(delay.count() * policy_.backoff_multiplier),
        policy_.max_delay.count()));
  }

  /// Execute a status-returning operation with retry
  template <typename Fn> core::Status execute_with_retry(Fn &&fn) {
    uint8_t attempts = 0;
    auto delay = policy_.initial_delay;

    while (true) {
      auto status = fn();

      if (status) {
        return status;
      }

      if (!is_retryable_error(status.error()) || !should_retry(attempts)) {
        return status;
      }

      wait_and_backoff(attempts, delay);
    }
  }

  /// Owned request data for async operations (survives caller scope)
  struct OwnedRequest {
    HttpMethod method;
    std::string path;
    std::vector<std::pair<std::string, std::string>> query_params;
    std::vector<uint8_t> body;
    ContentType content_type;

    /// Create owned copy from non-owning Request
    static OwnedRequest from(const Request &req) {
      OwnedRequest owned;
      owned.method = req.method;
      owned.path = std::string(req.path);
      owned.body = std::vector<uint8_t>(req.body.begin(), req.body.end());
      owned.content_type = req.content_type;

      owned.query_params.reserve(req.query_params.size());
      for (const auto &p : req.query_params) {
        owned.query_params.emplace_back(std::string(p.key),
                                        std::string(p.value));
      }
      return owned;
    }
  };

  /// State for async retry operations
  struct AsyncRetryState {
    OwnedRequest request;
    std::vector<QueryParam>
        query_param_views; // Views into request.query_params
    OnComplete callback;
    RetryPolicy policy;
    RetryTransport *transport;
    uint8_t attempts{0};
    std::chrono::milliseconds delay;

    AsyncRetryState(OwnedRequest req, OnComplete cb, const RetryPolicy &p,
                    RetryTransport *t)
        : request(std::move(req)), callback(std::move(cb)), policy(p),
          transport(t), delay(p.initial_delay) {
      // Build views pointing to owned data
      rebuild_views();
    }

    /// Rebuild query param views after move
    void rebuild_views() {
      query_param_views.clear();
      query_param_views.reserve(request.query_params.size());
      for (const auto &p : request.query_params) {
        query_param_views.push_back({p.first, p.second});
      }
    }

    /// Get Request with views into owned data
    [[nodiscard]] Request as_request() const {
      return Request{.method = request.method,
                     .path = request.path,
                     .query_params = query_param_views,
                     .body = request.body,
                     .content_type = request.content_type};
    }
  };

  /// Start an async attempt
  core::Status start_async_attempt(std::shared_ptr<AsyncRetryState> state) {
    auto retry_callback = [state](core::Result<Response> result) mutable {
      state->transport->handle_async_result(std::move(state),
                                            std::move(result));
    };

    return base_.send_async(state->as_request(), std::move(retry_callback));
  }

  /// Handle async result and potentially retry
  void handle_async_result(std::shared_ptr<AsyncRetryState> state,
                           core::Result<Response> result) {
    // Check if successful (but with server error that should retry)
    if (result) {
      if (result->is_server_error() && policy_.retry_on_server_error) {
        if (state->attempts < state->policy.max_retries) {
          schedule_retry(std::move(state));
          return;
        }
      }
      // Success or non-retryable response
      if (state->callback) {
        state->callback(std::move(result));
      }
      return;
    }

    // Check if error is retryable
    if (!is_retryable_error(result.error()) ||
        state->attempts >= state->policy.max_retries) {
      if (state->callback) {
        state->callback(std::move(result));
      }
      return;
    }

    // Schedule retry
    schedule_retry(std::move(state));
  }

  /// Schedule a retry after backoff delay
  void schedule_retry(std::shared_ptr<AsyncRetryState> state) {
    ESP_LOGW(TAG, "Async retry %d/%d after %lldms", state->attempts + 1,
             state->policy.max_retries, state->delay.count());

    // Create a one-shot timer for the retry
    auto timer_callback = [](TimerHandle_t timer) {
      auto *state_ptr = static_cast<std::shared_ptr<AsyncRetryState> *>(
          pvTimerGetTimerID(timer));
      auto state = std::move(*state_ptr);
      delete state_ptr;
      xTimerDelete(timer, 0);

      // Update retry state
      state->attempts++;
      state->delay = std::chrono::milliseconds(
          std::min(static_cast<int64_t>(state->delay.count() *
                                        state->policy.backoff_multiplier),
                   state->policy.max_delay.count()));

      // Start next attempt
      state->transport->start_async_attempt(std::move(state));
    };

    // Store state in heap for timer callback
    auto *state_ptr = new std::shared_ptr<AsyncRetryState>(std::move(state));

    TimerHandle_t timer =
        xTimerCreate("retry", pdMS_TO_TICKS((*state_ptr)->delay.count()),
                     pdFALSE, state_ptr, timer_callback);

    if (timer == nullptr) {
      // Timer creation failed - invoke callback with error
      if ((*state_ptr)->callback) {
        (*state_ptr)->callback(core::Err(ESP_ERR_NO_MEM));
      }
      delete state_ptr;
      return;
    }

    if (xTimerStart(timer, 0) != pdPASS) {
      // Timer start failed
      if ((*state_ptr)->callback) {
        (*state_ptr)->callback(core::Err(ESP_FAIL));
      }
      xTimerDelete(timer, 0);
      delete state_ptr;
    }
  }

  BaseTransport base_;
  RetryPolicy policy_;
};

/// Factory function for creating retry transport
/// @param base Base transport to wrap
/// @param policy Retry policy
/// @return RetryTransport wrapping the base
template <typename BaseTransport>
[[nodiscard]] auto make_retry_transport(BaseTransport base,
                                        const RetryPolicy &policy = {}) {
  return RetryTransport<BaseTransport>(std::move(base), policy);
}

} // namespace transport
