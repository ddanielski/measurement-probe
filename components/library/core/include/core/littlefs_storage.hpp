/**
 * @file littlefs_storage.hpp
 * @brief LittleFS storage backend implementation
 *
 * Uses files as key-value storage within namespace directories.
 */

#pragma once

#include "storage.hpp"
#include "storage_backend.hpp"

#include <esp_littlefs.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>

namespace core {

namespace lfs {
inline constexpr size_t MAX_PATH_LEN = 64;
inline constexpr const char *MOUNT_POINT = "/storage";
inline constexpr const char *PARTITION_LABEL = "storage";

/// RAII wrapper for FILE*
class FileHandle {
public:
  FileHandle(const char *path, const char *mode) : file_(fopen(path, mode)) {}
  ~FileHandle() {
    if (file_ != nullptr) {
      (void)fclose(file_);
    }
  }

  FileHandle(const FileHandle &) = delete;
  FileHandle &operator=(const FileHandle &) = delete;
  FileHandle(FileHandle &&other) noexcept : file_(other.file_) {
    other.file_ = nullptr;
  }
  FileHandle &operator=(FileHandle &&other) noexcept {
    if (this != &other) {
      if (file_ != nullptr) {
        (void)fclose(file_);
      }
      file_ = other.file_;
      other.file_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const { return file_ != nullptr; }
  [[nodiscard]] FILE *get() const { return file_; }

  /// Close and return success/failure
  [[nodiscard]] bool close() {
    if (file_ == nullptr) {
      return true;
    }
    int result = fclose(file_);
    file_ = nullptr;
    return result == 0;
  }

private:
  FILE *file_;
};
} // namespace lfs

/// LittleFS-based storage implementation (file per key)
class LittleFsStorage final : public IStorage {
public:
  ~LittleFsStorage() override = default;

  LittleFsStorage(const LittleFsStorage &) = delete;
  LittleFsStorage &operator=(const LittleFsStorage &) = delete;
  LittleFsStorage(LittleFsStorage &&) = default;
  LittleFsStorage &operator=(LittleFsStorage &&) = default;

  [[nodiscard]] bool is_ready() const override { return !base_path_.empty(); }

  [[nodiscard]] Result<size_t> get_blob_size(std::string_view key) override {
    auto path = make_path(key);
    struct stat file_stat{};
    if (stat(path.c_str(), &file_stat) != 0) {
      return Err(ESP_ERR_NOT_FOUND);
    }
    return static_cast<size_t>(file_stat.st_size);
  }

  [[nodiscard]] Status get_blob(std::string_view key,
                                std::span<uint8_t> buffer) override {
    lfs::FileHandle file(make_path(key).c_str(), "rb");
    if (!file.valid()) {
      return Err(ESP_ERR_NOT_FOUND);
    }

    size_t bytes_read = fread(buffer.data(), 1, buffer.size(), file.get());
    if (!file.close()) {
      return Err(ESP_FAIL);
    }

    return (bytes_read == buffer.size()) ? Ok() : Err(ESP_ERR_INVALID_SIZE);
  }

  [[nodiscard]] Status set_blob(std::string_view key,
                                std::span<const uint8_t> data) override {
    lfs::FileHandle file(make_path(key).c_str(), "wb");
    if (!file.valid()) {
      return Err(ESP_ERR_NO_MEM);
    }

    size_t written = fwrite(data.data(), 1, data.size(), file.get());
    if (!file.close()) {
      return Err(ESP_FAIL);
    }

    return (written == data.size()) ? Ok() : Err(ESP_FAIL);
  }

  [[nodiscard]] Result<size_t> get_string_size(std::string_view key) override {
    return get_blob_size(key);
  }

  [[nodiscard]] Status get_string(std::string_view key,
                                  std::span<char> buffer) override {
    auto result = get_blob(
        key, std::span<uint8_t>(reinterpret_cast<uint8_t *>(buffer.data()),
                                buffer.size()));
    return result;
  }

  /// @pre value must be null-terminated
  [[nodiscard]] Status set_string(std::string_view key,
                                  std::string_view value) override {
    lfs::FileHandle file(make_path(key).c_str(), "wb");
    if (!file.valid()) {
      return Err(ESP_ERR_NO_MEM);
    }

    // Include null terminator in stored data
    size_t total = value.size() + 1;
    size_t written = fwrite(value.data(), 1, total, file.get());
    if (!file.close()) {
      return Err(ESP_FAIL);
    }

    return (written == total) ? Ok() : Err(ESP_FAIL);
  }

  [[nodiscard]] bool contains(std::string_view key) override {
    auto path = make_path(key);
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
  }

  [[nodiscard]] Status erase(std::string_view key) override {
    auto path = make_path(key);
    return (remove(path.c_str()) == 0) ? Ok() : Err(ESP_ERR_NOT_FOUND);
  }

  [[nodiscard]] Status erase_all() override {
    DIR *dir = opendir(base_path_.c_str());
    if (dir == nullptr) {
      return Err(ESP_ERR_NOT_FOUND);
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_type == DT_REG) {
        std::string path = base_path_ + "/" + entry->d_name;
        (void)remove(path.c_str());
      }
    }
    closedir(dir);
    return Ok();
  }

  [[nodiscard]] Status commit() override {
    // LittleFS commits on each write, no explicit commit needed
    return Ok();
  }

protected:
  Result<int8_t> get_i8(std::string_view key) override {
    return get_scalar<int8_t>(key);
  }
  Result<uint8_t> get_u8(std::string_view key) override {
    return get_scalar<uint8_t>(key);
  }
  Result<int16_t> get_i16(std::string_view key) override {
    return get_scalar<int16_t>(key);
  }
  Result<uint16_t> get_u16(std::string_view key) override {
    return get_scalar<uint16_t>(key);
  }
  Result<int32_t> get_i32(std::string_view key) override {
    return get_scalar<int32_t>(key);
  }
  Result<uint32_t> get_u32(std::string_view key) override {
    return get_scalar<uint32_t>(key);
  }

  Status set_i8(std::string_view key, int8_t value) override {
    return set_scalar(key, value);
  }
  Status set_u8(std::string_view key, uint8_t value) override {
    return set_scalar(key, value);
  }
  Status set_i16(std::string_view key, int16_t value) override {
    return set_scalar(key, value);
  }
  Status set_u16(std::string_view key, uint16_t value) override {
    return set_scalar(key, value);
  }
  Status set_i32(std::string_view key, int32_t value) override {
    return set_scalar(key, value);
  }
  Status set_u32(std::string_view key, uint32_t value) override {
    return set_scalar(key, value);
  }

private:
  friend class LittleFsBackend;

  explicit LittleFsStorage(std::string base_path)
      : base_path_(std::move(base_path)) {}

  template <typename T> Result<T> get_scalar(std::string_view key) {
    T value{};
    auto status =
        get_blob(key, std::span<uint8_t>(reinterpret_cast<uint8_t *>(&value),
                                         sizeof(T)));
    if (!status) {
      return Err(status.error());
    }
    return value;
  }

  template <typename T> Status set_scalar(std::string_view key, T value) {
    return set_blob(
        key, std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&value),
                                      sizeof(T)));
  }

  [[nodiscard]] std::string make_path(std::string_view key) const {
    // File path: /storage/ns_key (flat structure, no directories)
    std::string path = base_path_;
    path += '_';
    path += key;
    return path;
  }

  std::string base_path_;
};

/// LittleFS storage backend
class LittleFsBackend final : public IStorageBackend {
public:
  LittleFsBackend() = default;
  ~LittleFsBackend() override { shutdown(); }

  LittleFsBackend(const LittleFsBackend &) = delete;
  LittleFsBackend &operator=(const LittleFsBackend &) = delete;
  LittleFsBackend(LittleFsBackend &&) = default;
  LittleFsBackend &operator=(LittleFsBackend &&) = default;

  [[nodiscard]] Status init() override {
    if (initialized_) {
      return Ok();
    }

    esp_vfs_littlefs_conf_t conf{};
    conf.base_path = lfs::MOUNT_POINT;
    conf.partition_label = lfs::PARTITION_LABEL;
    conf.partition = nullptr;
    conf.format_if_mount_failed = true;
    conf.read_only = false;
    conf.dont_mount = false;
    conf.grow_on_mount = true;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
      return Err(err);
    }

    initialized_ = true;
    return Ok();
  }

  [[nodiscard]] bool is_ready() const override { return initialized_; }

  [[nodiscard]] BackendId id() const override { return BackendId::LittleFs; }

  [[nodiscard]] StoragePtr open_namespace(NamespaceId ns) override {
    if (!initialized_) {
      return nullptr;
    }

    // Use flat file structure: /storage/ns_keyname
    // This avoids mkdir issues since LittleFS VFS may not support it
    std::string base_path = lfs::MOUNT_POINT;
    base_path += '/';
    base_path += namespace_name(ns);

    return std::unique_ptr<IStorage>(new LittleFsStorage(std::move(base_path)));
  }

  void shutdown() override {
    if (initialized_) {
      esp_vfs_littlefs_unregister(lfs::PARTITION_LABEL);
      initialized_ = false;
    }
  }

private:
  bool initialized_ = false;
};

} // namespace core
