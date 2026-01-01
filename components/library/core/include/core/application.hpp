/**
 * @file application.hpp
 * @brief Application base class - owns resources, abstracts platform details
 */

#pragma once

#include "event_loop.hpp"
#include "storage.hpp"

#include <cassert>

namespace core {

/// Application base - derive and implement run()
class Application {
public:
  virtual ~Application() = default;

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  Application(Application &&) = delete;
  Application &operator=(Application &&) = delete;

  /// Initialize and run the application
  [[nodiscard]] Status start() {
    if (auto err = init_platform(); !err.ok()) {
      return err;
    }
    run();
    return Status{};
  }

  /// Access storage (asserts if not initialized)
  [[nodiscard]] IStorage &storage() {
    assert(storage_ != nullptr && "Storage not initialized");
    return *storage_;
  }

  /// Access event bus
  [[nodiscard]] static EventBus &events() { return EventBus::get(); }

protected:
  Application() = default;

  /// Override to implement application logic
  virtual void run() = 0;

  /// Override to customize platform initialization
  virtual Status init_platform() {
    if (auto err = init_events(); !err.ok()) {
      return err;
    }
    return init_storage();
  }

  /// Initialize event bus
  static Status init_events() { return EventBus::initialize(); }

  /// Initialize storage subsystem
  Status init_storage() {
    esp_err_t err = StorageFactory::init_nvs_partition();
    if (err != ESP_OK) {
      return err;
    }

    storage_ = StorageFactory::create_nvs("app");
    if (storage_ == nullptr || !storage_->is_ready()) {
      return ESP_FAIL;
    }

    return Status{};
  }

  StoragePtr
      storage_; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
};

} // namespace core
