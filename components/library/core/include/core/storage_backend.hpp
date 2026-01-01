/**
 * @file storage_backend.hpp
 * @brief Storage backend interface with enum-based identification
 *
 * Backends implement this interface to provide storage capabilities.
 * Uses enums for fast comparison on embedded systems.
 */

#pragma once

#include "result.hpp"
#include "storage.hpp"

#include <memory>

namespace core {

/// Backend type identifier (compile-time, no string overhead)
enum class BackendId : uint8_t { Nvs, LittleFs, Spiffs, SdCard, Count };

/// Storage namespace identifier (define app-specific ones in config)
enum class NamespaceId : uint8_t { App, Bsec, Wifi, Count };

/// Abstract storage backend interface
class IStorageBackend {
public:
  virtual ~IStorageBackend() = default;

  IStorageBackend(const IStorageBackend &) = delete;
  IStorageBackend &operator=(const IStorageBackend &) = delete;
  IStorageBackend(IStorageBackend &&) = default;
  IStorageBackend &operator=(IStorageBackend &&) = default;

  /// Initialize the backend (mount filesystem, init partition, etc.)
  [[nodiscard]] virtual Status init() = 0;

  /// Check if backend is ready
  [[nodiscard]] virtual bool is_ready() const = 0;

  /// Get backend type
  [[nodiscard]] virtual BackendId id() const = 0;

  /// Open or create a namespace/directory for storage
  /// @param ns Namespace identifier
  /// @return Storage interface for this namespace, or nullptr on failure
  [[nodiscard]] virtual StoragePtr open_namespace(NamespaceId ns) = 0;

  /// Shutdown the backend (unmount, close handles, etc.)
  virtual void shutdown() = 0;

protected:
  IStorageBackend() = default;
};

using StorageBackendPtr = std::unique_ptr<IStorageBackend>;

/// Get string name for namespace (for NVS namespace names, logging)
constexpr const char *namespace_name(NamespaceId ns) {
  switch (ns) {
  case NamespaceId::App:
    return "app";
  case NamespaceId::Bsec:
    return "bsec";
  case NamespaceId::Wifi:
    return "wifi";
  default:
    return "unk";
  }
}

} // namespace core
