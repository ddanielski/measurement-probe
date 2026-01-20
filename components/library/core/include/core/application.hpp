/**
 * @file application.hpp
 * @brief Application base class - owns resources, abstracts platform details
 */

#pragma once

#include "event_loop.hpp"
#include "littlefs_storage.hpp"
#include "nvs_storage.hpp"
#include "storage_manager.hpp"

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
    if (auto err = init_platform(); !err) {
      return err;
    }
    run();
    return Ok();
  }

  /// Access storage manager
  [[nodiscard]] StorageManager &storage_manager() { return storage_manager_; }

  /// Access a storage namespace (convenience method)
  [[nodiscard]] IStorage &storage(NamespaceId ns = NamespaceId::App) {
    return storage_manager_.open(ns);
  }

  /// Access event bus
  [[nodiscard]] static EventBus &events() { return EventBus::get(); }

protected:
  Application() = default;

  /// Override to implement application logic
  virtual void run() = 0;

  /// Override to customize platform initialization
  virtual Status init_platform() {
    if (auto err = init_events(); !err) {
      return err;
    }
    return init_storage();
  }

  /// Initialize event bus
  static Status init_events() {
    esp_err_t err = EventBus::initialize();
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  /// Override to provide storage configuration
  /// Default: small data → NVS, large data → LittleFS
  [[nodiscard]] virtual StorageConfig get_storage_config() const {
    StorageConfig config{};
    config.map(NamespaceId::App, BackendId::Nvs);
    config.map(NamespaceId::Bsec, BackendId::Nvs);
    config.map(NamespaceId::Wifi, BackendId::Nvs);
    config.map(NamespaceId::Cloud, BackendId::Nvs);
    config.map(NamespaceId::Measurements, BackendId::LittleFs);
    return config;
  }

  /// Initialize storage subsystem
  Status init_storage() {
    // Add storage backends
    storage_manager_.add_backend(std::make_unique<NvsBackend>());
    storage_manager_.add_backend(std::make_unique<LittleFsBackend>());

    // Apply configuration
    storage_manager_.configure(get_storage_config());

    return storage_manager_.init();
  }

  StorageManager storage_manager_; // NOLINT
};

} // namespace core
