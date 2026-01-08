/**
 * @file data_manager.hpp
 * @brief DataManager - Coordinates sensor data routing
 *
 * Monitors push data here; DataManager decides where it goes:
 * - Cache (for read_all())
 * - Flash storage (future)
 * - Network queue (future)
 *
 * Memory: Uses fixed-size arrays and static mutex allocation.
 * Zero heap allocation - suitable for embedded systems.
 * For zero-allocation reads, use for_each() or read_into().
 *
 * Decoupled from SensorId enum - application provides sizing constants.
 */

#pragma once

#include "events.hpp"
#include "measurement.hpp"
#include "sensor.hpp"

#include <core/event_loop.hpp>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <array>
#include <span>

namespace sensor {

namespace {
constexpr const char *TAG = "data_mgr";
} // namespace

// ============================================================================
// IDataHandler - Interface for receiving measurement data
// ============================================================================

/// Interface for components that handle measurement data
/// Monitors call this when they have new data
class IDataHandler {
public:
  virtual ~IDataHandler() = default;

  IDataHandler(const IDataHandler &) = delete;
  IDataHandler &operator=(const IDataHandler &) = delete;
  IDataHandler(IDataHandler &&) = delete;
  IDataHandler &operator=(IDataHandler &&) = delete;

  /// Called when a sensor has new measurements
  /// @param sensor_id Raw sensor ID (from application's SensorId enum)
  virtual void on_data(SensorIdType sensor_id,
                       std::span<const Measurement> measurements) = 0;

protected:
  IDataHandler() = default;
};

// ============================================================================
// DataManager - Central data routing and caching
// ============================================================================

/// Manages sensor data caching and routing
/// Thread-safe for concurrent access from timer callbacks and app
///
/// @tparam MaxSensors Maximum number of sensor types (use SensorId::Count)
/// @tparam MaxMeasurementsPerSensor Maximum measurements per sensor
template <size_t MaxSensors, size_t MaxMeasurementsPerSensor = 16>
class DataManagerT : public IDataHandler {
public:
  static constexpr size_t SENSOR_COUNT = MaxSensors;
  static constexpr size_t MAX_MEASUREMENTS = MaxMeasurementsPerSensor;

  /// Constructor - uses statically allocated mutex (no heap)
  DataManagerT() : mutex_(xSemaphoreCreateMutexStatic(&mutex_buffer_)) {}

  ~DataManagerT() override = default;

  DataManagerT(const DataManagerT &) = delete;
  DataManagerT &operator=(const DataManagerT &) = delete;
  DataManagerT(DataManagerT &&) = delete;
  DataManagerT &operator=(DataManagerT &&) = delete;

  /// Called by monitors when they have new data
  void on_data(SensorIdType sensor_id,
               std::span<const Measurement> measurements) override {
    Lock lock(mutex_);

    auto idx = static_cast<size_t>(sensor_id);
    if (idx >= SENSOR_COUNT) {
      ESP_LOGW(TAG, "Sensor ID %u out of range (max: %zu)", sensor_id,
               SENSOR_COUNT);
      return;
    }

    auto &entry = cache_.at(idx);
    entry.count = std::min(measurements.size(), MAX_MEASUREMENTS);
    std::copy_n(measurements.begin(), entry.count, entry.data.begin());
    entry.valid = true;

    // Publish event so consumers can react without polling
    SensorDataEvent evt{sensor_id, entry.count};
    (void)core::events().publish(SENSOR_EVENTS, SensorEvent::DataReady, &evt);
    // Future: could also write to flash, queue for network, etc.
  }

  // ==========================================================================
  // Zero-allocation read methods (preferred for embedded)
  // ==========================================================================

  /// Read into caller-provided buffer (zero allocation)
  /// @return Number of measurements written
  [[nodiscard]] size_t read_into(SensorIdType sensor_id,
                                 std::span<Measurement> out) const {
    Lock lock(mutex_);

    auto idx = static_cast<size_t>(sensor_id);
    if (idx >= SENSOR_COUNT || !cache_.at(idx).valid) {
      return 0;
    }

    const auto &entry = cache_.at(idx);
    size_t to_copy = std::min(entry.count, out.size());
    std::copy_n(entry.data.begin(), to_copy, out.begin());
    return to_copy;
  }

  /// Read all into caller-provided buffer (zero allocation)
  /// @return Number of measurements written
  [[nodiscard]] size_t read_all_into(std::span<Measurement> out) const {
    Lock lock(mutex_);
    size_t written = 0;

    for (const auto &entry : cache_) {
      if (entry.valid) {
        size_t to_copy = std::min(entry.count, out.size() - written);
        std::copy_n(entry.data.begin(), to_copy,
                    out.begin() + static_cast<std::ptrdiff_t>(written));
        written += to_copy;
        if (written >= out.size()) {
          break;
        }
      }
    }
    return written;
  }

  /// Visit each measurement (zero allocation)
  /// Callback: void(const Measurement&)
  template <typename Func> void for_each(const Func &callback) const {
    Lock lock(mutex_);
    for (const auto &entry : cache_) {
      if (entry.valid) {
        for (size_t i = 0; i < entry.count; ++i) {
          callback(entry.data[i]);
        }
      }
    }
  }

  /// Visit measurements for a specific sensor (zero allocation)
  template <typename Func>
  void for_each(SensorIdType sensor_id, const Func &callback) const {
    Lock lock(mutex_);
    auto idx = static_cast<size_t>(sensor_id);
    if (idx >= SENSOR_COUNT || !cache_.at(idx).valid) {
      return;
    }
    const auto &entry = cache_.at(idx);
    for (size_t i = 0; i < entry.count; ++i) {
      callback(entry.data[i]);
    }
  }

  /// Get total measurement count across all sensors
  [[nodiscard]] size_t total_measurement_count() const {
    Lock lock(mutex_);
    size_t total = 0;
    for (const auto &entry : cache_) {
      if (entry.valid) {
        total += entry.count;
      }
    }
    return total;
  }

  /// Get number of sensors with cached data
  [[nodiscard]] size_t sensor_count() const {
    Lock lock(mutex_);
    size_t count = 0;
    for (const auto &entry : cache_) {
      if (entry.valid) {
        ++count;
      }
    }
    return count;
  }

  /// Clear all cached data
  void clear() {
    Lock lock(mutex_);
    for (auto &entry : cache_) {
      entry.valid = false;
      entry.count = 0;
    }
  }

private:
  /// RAII lock guard for FreeRTOS mutex
  class Lock {
  public:
    explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
      if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
      }
    }
    ~Lock() {
      if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
      }
    }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    Lock(Lock &&) = delete;
    Lock &operator=(Lock &&) = delete;

  private:
    SemaphoreHandle_t mutex_;
  };

  /// Cache entry for one sensor
  struct CacheEntry {
    std::array<Measurement, MAX_MEASUREMENTS> data{};
    size_t count = 0;
    bool valid = false;
  };

  /// Static buffer for mutex (no heap allocation)
  mutable StaticSemaphore_t mutex_buffer_{};
  mutable SemaphoreHandle_t mutex_ = nullptr;
  std::array<CacheEntry, SENSOR_COUNT> cache_{};
};

/// Convenience alias - application should define with its SensorId::Count
/// Example: using DataManager = DataManagerT<sensor_type_count()>;
template <size_t MaxSensors> using DataManager = DataManagerT<MaxSensors>;

} // namespace sensor
