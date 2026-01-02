/**
 * @file storage_manager.hpp
 * @brief Unified storage manager with multi-backend support
 *
 * StorageManager coordinates multiple storage backends and routes
 * namespace requests to the appropriate backend based on configuration.
 * Uses enums for efficient embedded operation.
 */

#pragma once

#include "result.hpp"
#include "storage.hpp"
#include "storage_backend.hpp"

#include <array>
#include <cassert>
#include <memory>

namespace core {

/// Namespace to backend mapping entry
struct NamespaceMapping {
  NamespaceId ns;
  BackendId backend;
};

/// Storage manager configuration
struct StorageConfig {
  /// Namespace to backend mappings (terminated by ns == NamespaceId::Count)
  std::array<NamespaceMapping, static_cast<size_t>(NamespaceId::Count)>
      mappings{};
  size_t mapping_count = 0;

  /// Add a mapping
  constexpr void map(NamespaceId ns, BackendId backend) {
    if (mapping_count < mappings.size()) {
      mappings.at(mapping_count++) = {.ns = ns, .backend = backend};
    }
  }
};

/// Storage manager - coordinates multiple backends
class StorageManager {
public:
  StorageManager() = default;
  ~StorageManager() { shutdown(); }

  StorageManager(const StorageManager &) = delete;
  StorageManager &operator=(const StorageManager &) = delete;
  StorageManager(StorageManager &&) = default;
  StorageManager &operator=(StorageManager &&) = default;

  /// Add a storage backend
  void add_backend(StorageBackendPtr backend) {
    if (backend == nullptr) {
      return;
    }
    auto idx = static_cast<size_t>(backend->id());
    if (idx < backends_.size()) {
      backends_.at(idx) = std::move(backend);
    }
  }

  /// Apply configuration (namespace mappings)
  void configure(const StorageConfig &config) {
    for (size_t i = 0; i < config.mapping_count; ++i) {
      const auto &mapping = config.mappings.at(i);
      auto ns_idx = static_cast<size_t>(mapping.ns);
      if (ns_idx < namespace_map_.size()) {
        namespace_map_.at(ns_idx) = mapping.backend;
      }
    }
  }

  /// Initialize all backends
  [[nodiscard]] Status init() {
    for (auto &backend : backends_) {
      if (backend != nullptr) {
        if (auto err = backend->init(); !err) {
          return err;
        }
      }
    }
    initialized_ = true;
    return Ok();
  }

  /// Check if manager is initialized
  [[nodiscard]] bool is_ready() const { return initialized_; }

  /// Open a namespace (creates if needed, caches for reuse)
  [[nodiscard]] IStorage &open(NamespaceId ns) {
    auto ns_idx = static_cast<size_t>(ns);
    assert(ns_idx < open_namespaces_.size() && "Invalid namespace");

    // Check cache first
    auto &cached = open_namespaces_.at(ns_idx);
    if (cached != nullptr) {
      return *cached;
    }

    // Find backend for this namespace
    BackendId backend_id = namespace_map_.at(ns_idx);
    auto backend_idx = static_cast<size_t>(backend_id);

    assert(backend_idx < backends_.size() && "Invalid backend");
    auto &backend = backends_.at(backend_idx);
    assert(backend != nullptr && "Backend not registered");

    // Open namespace on backend
    auto storage = backend->open_namespace(ns);
    assert(storage != nullptr && "Failed to open namespace");

    auto &ref = *storage;
    cached = std::move(storage);
    return ref;
  }

  /// Commit all open namespaces
  void commit_all() {
    for (auto &storage : open_namespaces_) {
      if (storage != nullptr) {
        (void)storage->commit();
      }
    }
  }

  /// Shutdown all backends
  void shutdown() {
    for (auto &storage : open_namespaces_) {
      storage.reset();
    }
    for (auto &backend : backends_) {
      if (backend != nullptr) {
        backend->shutdown();
      }
    }
    initialized_ = false;
  }

  /// Get a backend by ID (for advanced use)
  [[nodiscard]] IStorageBackend *get_backend(BackendId id) {
    auto idx = static_cast<size_t>(id);
    return (idx < backends_.size()) ? backends_.at(idx).get() : nullptr;
  }

private:
  static constexpr size_t kMaxBackends = static_cast<size_t>(BackendId::Count);
  static constexpr size_t kMaxNamespaces =
      static_cast<size_t>(NamespaceId::Count);

  bool initialized_ = false;

  std::array<StorageBackendPtr, kMaxBackends> backends_{};
  std::array<BackendId, kMaxNamespaces> namespace_map_{};
  std::array<StoragePtr, kMaxNamespaces> open_namespaces_{};
};

} // namespace core
