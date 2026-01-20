/**
 * @file credentials.hpp
 * @brief Device credentials management
 *
 * Handles loading/saving device credentials (device_id, secret) from NVS.
 * These are provisioned during factory setup and never change.
 */

#pragma once

#include <core/result.hpp>
#include <core/storage.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace cloud {

/// Device credential storage keys
namespace keys {
inline constexpr std::string_view DEVICE_ID = "device_id";
inline constexpr std::string_view SECRET = "secret";
} // namespace keys

/// Device credential sizes
namespace sizes {
/// UUID format: 550e8400-e29b-41d4-a716-446655440000 (36 chars + null)
inline constexpr size_t DEVICE_ID_SIZE = 37;
/// 64-character hex string + null
inline constexpr size_t SECRET_SIZE = 65;
} // namespace sizes

/// Device credentials loaded from NVS
struct DeviceCredentials {
  std::array<char, sizes::DEVICE_ID_SIZE> device_id{};
  std::array<char, sizes::SECRET_SIZE> secret{};

  [[nodiscard]] bool is_valid() const {
    return device_id[0] != '\0' && secret[0] != '\0';
  }

  [[nodiscard]] std::string_view device_id_view() const {
    return {device_id.data()};
  }

  [[nodiscard]] std::string_view secret_view() const { return {secret.data()}; }
};

/// Load device credentials from storage
[[nodiscard]] inline core::Result<DeviceCredentials>
load_credentials(core::IStorage &storage) {
  DeviceCredentials creds{};

  auto id_size = storage.get_string_size(keys::DEVICE_ID);
  if (!id_size || *id_size == 0) {
    return core::Err(ESP_ERR_NOT_FOUND);
  }
  if (auto err = storage.get_string(
          keys::DEVICE_ID, {creds.device_id.data(), creds.device_id.size()});
      !err) {
    return core::Err(err.error());
  }

  auto secret_size = storage.get_string_size(keys::SECRET);
  if (!secret_size || *secret_size == 0) {
    return core::Err(ESP_ERR_NOT_FOUND);
  }
  if (auto err = storage.get_string(keys::SECRET,
                                    {creds.secret.data(), creds.secret.size()});
      !err) {
    return core::Err(err.error());
  }

  return creds;
}

/// Save device credentials to storage (used during provisioning)
[[nodiscard]] inline core::Status save_credentials(core::IStorage &storage,
                                                   std::string_view device_id,
                                                   std::string_view secret) {
  auto guard = storage.auto_commit();

  if (auto err = storage.set_string(keys::DEVICE_ID, device_id); !err) {
    return err;
  }

  if (auto err = storage.set_string(keys::SECRET, secret); !err) {
    return err;
  }

  return core::Ok();
}

/// Check if device is provisioned
[[nodiscard]] inline bool is_provisioned(core::IStorage &storage) {
  return storage.contains(keys::DEVICE_ID) && storage.contains(keys::SECRET);
}

/// Clear device credentials (factory reset)
[[nodiscard]] inline core::Status clear_credentials(core::IStorage &storage) {
  auto guard = storage.auto_commit();
  (void)storage.erase(keys::DEVICE_ID);
  (void)storage.erase(keys::SECRET);
  return core::Ok();
}

} // namespace cloud
