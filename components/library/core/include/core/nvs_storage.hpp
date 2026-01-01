/**
 * @file nvs_storage.hpp
 * @brief NVS storage backend
 *
 * @warning NVS has limited write endurance (~100k cycles).
 *          Use for config/calibration, not frequently-changing data.
 */

#pragma once

#include "storage.hpp"

#include <nvs.h>
#include <nvs_flash.h>

#include <array>
#include <cstring>

namespace core {

class NvsStorage final : public IStorage {
public:
  ~NvsStorage() override {
    if (handle_ != 0)
      nvs_close(handle_);
  }

  NvsStorage(NvsStorage &&other) noexcept : handle_(other.handle_) {
    other.handle_ = 0;
  }

  NvsStorage &operator=(NvsStorage &&other) noexcept {
    if (this != &other) {
      if (handle_ != 0)
        nvs_close(handle_);
      handle_ = other.handle_;
      other.handle_ = 0;
    }
    return *this;
  }

  [[nodiscard]] bool is_ready() const override { return handle_ != 0; }

  [[nodiscard]] Result<size_t> get_blob_size(std::string_view key) override {
    size_t size = 0;
    if (auto err = nvs_get_blob(handle_, make_key(key).data(), nullptr, &size);
        err != ESP_OK)
      return err;
    return size;
  }

  [[nodiscard]] Status get_blob(std::string_view key,
                                std::span<uint8_t> buffer) override {
    size_t size = buffer.size();
    return nvs_get_blob(handle_, make_key(key).data(), buffer.data(), &size);
  }

  [[nodiscard]] Status set_blob(std::string_view key,
                                std::span<const uint8_t> data) override {
    return nvs_set_blob(handle_, make_key(key).data(), data.data(),
                        data.size());
  }

  [[nodiscard]] Result<size_t> get_string_size(std::string_view key) override {
    size_t size = 0;
    if (auto err = nvs_get_str(handle_, make_key(key).data(), nullptr, &size);
        err != ESP_OK)
      return err;
    return size;
  }

  [[nodiscard]] Status get_string(std::string_view key,
                                  std::span<char> buffer) override {
    size_t size = buffer.size();
    return nvs_get_str(handle_, make_key(key).data(), buffer.data(), &size);
  }

  [[nodiscard]] Status set_string(std::string_view key,
                                  std::string_view value) override {
    std::array<char, 512> buf{};
    if (value.size() >= buf.size())
      return ESP_ERR_INVALID_SIZE;
    std::memcpy(buf.data(), value.data(), value.size());
    return nvs_set_str(handle_, make_key(key).data(), buf.data());
  }

  [[nodiscard]] bool contains(std::string_view key) override {
    nvs_type_t type;
    return nvs_find_key(handle_, make_key(key).data(), &type) == ESP_OK;
  }

  [[nodiscard]] Status erase(std::string_view key) override {
    return nvs_erase_key(handle_, make_key(key).data());
  }

  [[nodiscard]] Status commit() override { return nvs_commit(handle_); }

protected:
  Result<int8_t> get_i8(std::string_view key) override {
    int8_t v{};
    if (auto e = nvs_get_i8(handle_, make_key(key).data(), &v); e != ESP_OK)
      return e;
    return v;
  }
  Result<uint8_t> get_u8(std::string_view key) override {
    uint8_t v{};
    if (auto e = nvs_get_u8(handle_, make_key(key).data(), &v); e != ESP_OK)
      return e;
    return v;
  }
  Result<int16_t> get_i16(std::string_view key) override {
    int16_t v{};
    if (auto e = nvs_get_i16(handle_, make_key(key).data(), &v); e != ESP_OK)
      return e;
    return v;
  }
  Result<uint16_t> get_u16(std::string_view key) override {
    uint16_t v{};
    if (auto e = nvs_get_u16(handle_, make_key(key).data(), &v); e != ESP_OK)
      return e;
    return v;
  }
  Result<int32_t> get_i32(std::string_view key) override {
    int32_t v{};
    if (auto e = nvs_get_i32(handle_, make_key(key).data(), &v); e != ESP_OK)
      return e;
    return v;
  }
  Result<uint32_t> get_u32(std::string_view key) override {
    uint32_t v{};
    if (auto e = nvs_get_u32(handle_, make_key(key).data(), &v); e != ESP_OK)
      return e;
    return v;
  }

  Status set_i8(std::string_view key, int8_t value) override {
    return nvs_set_i8(handle_, make_key(key).data(), value);
  }
  Status set_u8(std::string_view key, uint8_t value) override {
    return nvs_set_u8(handle_, make_key(key).data(), value);
  }
  Status set_i16(std::string_view key, int16_t value) override {
    return nvs_set_i16(handle_, make_key(key).data(), value);
  }
  Status set_u16(std::string_view key, uint16_t value) override {
    return nvs_set_u16(handle_, make_key(key).data(), value);
  }
  Status set_i32(std::string_view key, int32_t value) override {
    return nvs_set_i32(handle_, make_key(key).data(), value);
  }
  Status set_u32(std::string_view key, uint32_t value) override {
    return nvs_set_u32(handle_, make_key(key).data(), value);
  }

private:
  friend class StorageFactory;
  explicit NvsStorage(nvs_handle_t handle) : handle_(handle) {}

  static std::array<char, 16> make_key(std::string_view key) {
    std::array<char, 16> buf{};
    std::memcpy(buf.data(), key.data(), std::min(key.size(), buf.size() - 1));
    return buf;
  }

  nvs_handle_t handle_ = 0;
};

inline esp_err_t StorageFactory::init_nvs_partition() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if ((err = nvs_flash_erase()) != ESP_OK)
      return err;
    err = nvs_flash_init();
  }
  return err;
}

inline StoragePtr StorageFactory::create_nvs(std::string_view ns_name) {
  std::array<char, 16> ns_buf{};
  std::memcpy(ns_buf.data(), ns_name.data(),
              std::min(ns_name.size(), ns_buf.size() - 1));

  nvs_handle_t handle;
  if (nvs_open(ns_buf.data(), NVS_READWRITE, &handle) != ESP_OK)
    return nullptr;

  return std::unique_ptr<IStorage>(new NvsStorage(handle));
}

} // namespace core
