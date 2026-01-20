/**
 * @file nvs_storage.hpp
 * @brief NVS storage backend implementation
 *
 */

#pragma once

#include "storage.hpp"
#include "storage_backend.hpp"

#include <nvs.h>
#include <nvs_flash.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace core {

/// NVS limits
namespace nvs {
/// Maximum NVS key length (15 chars + null terminator)
inline constexpr size_t MAX_KEY_LEN = 15;
/// Key buffer size (includes null terminator)
inline constexpr size_t KEY_BUFFER_SIZE = MAX_KEY_LEN + 1;
} // namespace nvs

/// NVS-based storage implementation
class NvsStorage final : public IStorage {
public:
  ~NvsStorage() override {
    if (handle_ != 0) {
      nvs_close(handle_);
    }
  }

  NvsStorage(const NvsStorage &) = delete;
  NvsStorage &operator=(const NvsStorage &) = delete;

  NvsStorage(NvsStorage &&other) noexcept : handle_(other.handle_) {
    other.handle_ = 0;
  }

  NvsStorage &operator=(NvsStorage &&other) noexcept {
    if (this != &other) {
      if (handle_ != 0) {
        nvs_close(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = 0;
    }
    return *this;
  }

  [[nodiscard]] bool is_ready() const override { return handle_ != 0; }

  [[nodiscard]] Result<size_t> get_blob_size(std::string_view key) override {
    size_t size = 0;
    if (auto err = nvs_get_blob(handle_, make_key(key).data(), nullptr, &size);
        err != ESP_OK) {
      return Err(err);
    }
    return size;
  }

  [[nodiscard]] Status get_blob(std::string_view key,
                                std::span<uint8_t> buffer) override {
    size_t size = buffer.size();
    esp_err_t err =
        nvs_get_blob(handle_, make_key(key).data(), buffer.data(), &size);
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  [[nodiscard]] Status set_blob(std::string_view key,
                                std::span<const uint8_t> data) override {
    esp_err_t err =
        nvs_set_blob(handle_, make_key(key).data(), data.data(), data.size());
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  [[nodiscard]] Result<size_t> get_string_size(std::string_view key) override {
    size_t size = 0;
    if (auto err = nvs_get_str(handle_, make_key(key).data(), nullptr, &size);
        err != ESP_OK) {
      return Err(err);
    }
    return size;
  }

  [[nodiscard]] Status get_string(std::string_view key,
                                  std::span<char> buffer) override {
    size_t size = buffer.size();
    esp_err_t err =
        nvs_get_str(handle_, make_key(key).data(), buffer.data(), &size);
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  /// @pre value must be null-terminated
  [[nodiscard]] Status set_string(std::string_view key,
                                  std::string_view value) override {
    esp_err_t err = nvs_set_str(handle_, make_key(key).data(), value.data());
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  [[nodiscard]] bool contains(std::string_view key) override {
    nvs_type_t type{};
    return nvs_find_key(handle_, make_key(key).data(), &type) == ESP_OK;
  }

  [[nodiscard]] Status erase(std::string_view key) override {
    esp_err_t err = nvs_erase_key(handle_, make_key(key).data());
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  [[nodiscard]] Status erase_all() override {
    esp_err_t err = nvs_erase_all(handle_);
    if (err == ESP_OK) {
      err = nvs_commit(handle_);
    }
    return (err == ESP_OK) ? Ok() : Err(err);
  }

  [[nodiscard]] Status commit() override {
    esp_err_t err = nvs_commit(handle_);
    return (err == ESP_OK) ? Ok() : Err(err);
  }

protected:
  Result<int8_t> get_i8(std::string_view key) override {
    int8_t val{};
    if (auto err = nvs_get_i8(handle_, make_key(key).data(), &val);
        err != ESP_OK) {
      return Err(err);
    }
    return val;
  }
  Result<uint8_t> get_u8(std::string_view key) override {
    uint8_t val{};
    if (auto err = nvs_get_u8(handle_, make_key(key).data(), &val);
        err != ESP_OK) {
      return Err(err);
    }
    return val;
  }
  Result<int16_t> get_i16(std::string_view key) override {
    int16_t val{};
    if (auto err = nvs_get_i16(handle_, make_key(key).data(), &val);
        err != ESP_OK) {
      return Err(err);
    }
    return val;
  }
  Result<uint16_t> get_u16(std::string_view key) override {
    uint16_t val{};
    if (auto err = nvs_get_u16(handle_, make_key(key).data(), &val);
        err != ESP_OK) {
      return Err(err);
    }
    return val;
  }
  Result<int32_t> get_i32(std::string_view key) override {
    int32_t val{};
    if (auto err = nvs_get_i32(handle_, make_key(key).data(), &val);
        err != ESP_OK) {
      return Err(err);
    }
    return val;
  }
  Result<uint32_t> get_u32(std::string_view key) override {
    uint32_t val{};
    if (auto err = nvs_get_u32(handle_, make_key(key).data(), &val);
        err != ESP_OK) {
      return Err(err);
    }
    return val;
  }

  Status set_i8(std::string_view key, int8_t value) override {
    esp_err_t err = nvs_set_i8(handle_, make_key(key).data(), value);
    return (err == ESP_OK) ? Ok() : Err(err);
  }
  Status set_u8(std::string_view key, uint8_t value) override {
    esp_err_t err = nvs_set_u8(handle_, make_key(key).data(), value);
    return (err == ESP_OK) ? Ok() : Err(err);
  }
  Status set_i16(std::string_view key, int16_t value) override {
    esp_err_t err = nvs_set_i16(handle_, make_key(key).data(), value);
    return (err == ESP_OK) ? Ok() : Err(err);
  }
  Status set_u16(std::string_view key, uint16_t value) override {
    esp_err_t err = nvs_set_u16(handle_, make_key(key).data(), value);
    return (err == ESP_OK) ? Ok() : Err(err);
  }
  Status set_i32(std::string_view key, int32_t value) override {
    esp_err_t err = nvs_set_i32(handle_, make_key(key).data(), value);
    return (err == ESP_OK) ? Ok() : Err(err);
  }
  Status set_u32(std::string_view key, uint32_t value) override {
    esp_err_t err = nvs_set_u32(handle_, make_key(key).data(), value);
    return (err == ESP_OK) ? Ok() : Err(err);
  }

private:
  friend class NvsBackend;

  explicit NvsStorage(nvs_handle_t handle) : handle_(handle) {}

  static std::array<char, nvs::KEY_BUFFER_SIZE> make_key(std::string_view key) {
    std::array<char, nvs::KEY_BUFFER_SIZE> buf{};
    std::memcpy(buf.data(), key.data(), std::min(key.size(), nvs::MAX_KEY_LEN));
    return buf;
  }

  nvs_handle_t handle_ = 0;
};

/// NVS storage backend
class NvsBackend final : public IStorageBackend {
public:
  NvsBackend() = default;
  ~NvsBackend() override { shutdown(); }

  NvsBackend(const NvsBackend &) = delete;
  NvsBackend &operator=(const NvsBackend &) = delete;
  NvsBackend(NvsBackend &&) = default;
  NvsBackend &operator=(NvsBackend &&) = default;

  [[nodiscard]] Status init() override {
    if (initialized_) {
      return Ok();
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      err = nvs_flash_erase();
      if (err != ESP_OK) {
        return Err(err);
      }
      err = nvs_flash_init();
    }

    if (err != ESP_OK) {
      return Err(err);
    }

    initialized_ = true;
    return Ok();
  }

  [[nodiscard]] bool is_ready() const override { return initialized_; }

  [[nodiscard]] BackendId id() const override { return BackendId::Nvs; }

  [[nodiscard]] StoragePtr open_namespace(NamespaceId ns) override {
    if (!initialized_) {
      return nullptr;
    }

    const char *ns_name = namespace_name(ns);
    nvs_handle_t handle = 0;
    if (nvs_open(ns_name, NVS_READWRITE, &handle) != ESP_OK) {
      return nullptr;
    }

    return std::unique_ptr<IStorage>(new NvsStorage(handle));
  }

  void shutdown() override {
    if (initialized_) {
      initialized_ = false;
    }
  }

private:
  bool initialized_ = false;
};

} // namespace core
