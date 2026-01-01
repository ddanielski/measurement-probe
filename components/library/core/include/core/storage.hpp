/**
 * @file storage.hpp
 * @brief Storage abstraction with factory pattern
 */

#pragma once

#include "result.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

namespace core {

/// Types that can be stored as scalars
template <typename T>
concept Storable = std::is_arithmetic_v<T> || std::is_enum_v<T>;

class IStorage;

/// RAII commit guard - commits on destruction
class CommitGuard {
public:
  explicit CommitGuard(IStorage &storage) : storage_(storage) {}
  ~CommitGuard();

  CommitGuard(const CommitGuard &) = delete;
  CommitGuard &operator=(const CommitGuard &) = delete;
  CommitGuard(CommitGuard &&other) noexcept
      : storage_(other.storage_), released_(other.released_) {
    other.released_ = true;
  }
  CommitGuard &operator=(CommitGuard &&) = delete;

  void release() { released_ = true; }

private:
  IStorage &storage_;
  bool released_ = false;
};

/// Abstract key-value storage interface
class IStorage {
public:
  virtual ~IStorage() = default;

  IStorage(const IStorage &) = delete;
  IStorage &operator=(const IStorage &) = delete;
  IStorage(IStorage &&) = default;
  IStorage &operator=(IStorage &&) = default;

  [[nodiscard]] virtual bool is_ready() const = 0;

  /// Get value by key
  template <Storable T> [[nodiscard]] Result<T> get(std::string_view key) {
    if constexpr (std::is_same_v<T, int32_t>) {
      return get_i32(key);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      return get_u32(key);
    } else if constexpr (std::is_same_v<T, int16_t>) {
      return get_i16(key);
    } else if constexpr (std::is_same_v<T, uint16_t>) {
      return get_u16(key);
    } else if constexpr (std::is_same_v<T, int8_t>) {
      return get_i8(key);
    } else if constexpr (std::is_same_v<T, uint8_t>) {
      return get_u8(key);
    } else if constexpr (std::is_enum_v<T>) {
      auto r = get<std::underlying_type_t<T>>(key);
      if (!r)
        return r.error();
      return static_cast<T>(*r);
    } else {
      static_assert(sizeof(T) == 0, "Unsupported type");
    }
  }

  /// Set value by key
  template <Storable T>
  [[nodiscard]] Status set(std::string_view key, T value) {
    if constexpr (std::is_same_v<T, int32_t>) {
      return set_i32(key, value);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      return set_u32(key, value);
    } else if constexpr (std::is_same_v<T, int16_t>) {
      return set_i16(key, value);
    } else if constexpr (std::is_same_v<T, uint16_t>) {
      return set_u16(key, value);
    } else if constexpr (std::is_same_v<T, int8_t>) {
      return set_i8(key, value);
    } else if constexpr (std::is_same_v<T, uint8_t>) {
      return set_u8(key, value);
    } else if constexpr (std::is_enum_v<T>) {
      return set<std::underlying_type_t<T>>(
          key, static_cast<std::underlying_type_t<T>>(value));
    } else {
      static_assert(sizeof(T) == 0, "Unsupported type");
    }
  }

  /// Create RAII commit guard
  [[nodiscard]] CommitGuard auto_commit() { return CommitGuard(*this); }

  [[nodiscard]] virtual Result<size_t> get_blob_size(std::string_view key) = 0;
  [[nodiscard]] virtual Status get_blob(std::string_view key,
                                        std::span<uint8_t> buffer) = 0;
  [[nodiscard]] virtual Status set_blob(std::string_view key,
                                        std::span<const uint8_t> data) = 0;

  [[nodiscard]] virtual Result<size_t>
  get_string_size(std::string_view key) = 0;
  [[nodiscard]] virtual Status get_string(std::string_view key,
                                          std::span<char> buffer) = 0;
  [[nodiscard]] virtual Status set_string(std::string_view key,
                                          std::string_view value) = 0;

  [[nodiscard]] virtual bool contains(std::string_view key) = 0;
  [[nodiscard]] virtual Status erase(std::string_view key) = 0;
  [[nodiscard]] virtual Status commit() = 0;

protected:
  IStorage() = default;

  virtual Result<int8_t> get_i8(std::string_view key) = 0;
  virtual Result<uint8_t> get_u8(std::string_view key) = 0;
  virtual Result<int16_t> get_i16(std::string_view key) = 0;
  virtual Result<uint16_t> get_u16(std::string_view key) = 0;
  virtual Result<int32_t> get_i32(std::string_view key) = 0;
  virtual Result<uint32_t> get_u32(std::string_view key) = 0;

  virtual Status set_i8(std::string_view key, int8_t value) = 0;
  virtual Status set_u8(std::string_view key, uint8_t value) = 0;
  virtual Status set_i16(std::string_view key, int16_t value) = 0;
  virtual Status set_u16(std::string_view key, uint16_t value) = 0;
  virtual Status set_i32(std::string_view key, int32_t value) = 0;
  virtual Status set_u32(std::string_view key, uint32_t value) = 0;
};

inline CommitGuard::~CommitGuard() {
  if (!released_) {
    storage_.commit();
  }
}

using StoragePtr = std::unique_ptr<IStorage>;

/// Creates storage backends
class StorageFactory {
public:
  [[nodiscard]] static StoragePtr create_nvs(std::string_view ns_name);
  [[nodiscard]] static esp_err_t init_nvs_partition();
};

} // namespace core
