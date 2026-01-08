/**
 * @file manager.hpp
 * @brief SensorManager - Aggregates sensor monitors
 *
 * Uses DataManager for centralized data routing.
 * Memory: Fixed-size storage, no heap allocation for monitor pointers.
 *
 * Decoupled from SensorId enum - uses SensorIdType (uint8_t).
 *
 * @note register_monitor() AUTO-STARTS the monitor after attaching the data
 * handler. This ensures monitors always have a valid handler before sampling.
 * Use add_monitor() if you need to configure the monitor before starting.
 */

#pragma once

#include "monitor.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace sensor {

/// Maximum number of monitors (compile-time fixed)
inline constexpr size_t MAX_MONITORS = 8;

/// Manages sensor monitors and provides aggregated read via DataManager
/// Monitors must outlive the manager (caller owns them)
///
/// @tparam DataManagerType The DataManager type (e.g.,
/// DataManagerT<SensorCount>)
template <typename DataManagerType> class SensorManagerT {
public:
  /// Construct with a DataManager (required for data routing)
  explicit SensorManagerT(DataManagerType &data_manager)
      : data_manager_(data_manager) {}

  ~SensorManagerT() = default;

  SensorManagerT(const SensorManagerT &) = delete;
  SensorManagerT &operator=(const SensorManagerT &) = delete;
  SensorManagerT(SensorManagerT &&) = delete;
  SensorManagerT &operator=(SensorManagerT &&) = delete;

  /// Register a monitor and AUTO-START it (caller retains ownership)
  ///
  /// This method:
  /// 1. Attaches the DataManager as the data handler
  /// 2. Adds the monitor to the managed collection
  /// 3. Immediately starts the monitor
  ///
  /// Use add_monitor() if you need to configure the monitor (e.g., set error
  /// handler) before starting.
  ///
  /// @param monitor Monitor to register (must outlive this manager)
  /// @return true if registered, false if at capacity
  bool register_monitor(IMonitor &monitor) {
    if (!add_monitor(monitor)) {
      return false;
    }
    monitor.start();
    return true;
  }

  /// Add a monitor WITHOUT starting it (caller retains ownership)
  ///
  /// Use this when you need to configure the monitor before starting:
  /// @code
  ///   manager.add_monitor(my_monitor);
  ///   my_monitor.set_error_handler([](auto& err) { ... });
  ///   my_monitor.start();  // Manual start after configuration
  /// @endcode
  ///
  /// @param monitor Monitor to add (must outlive this manager)
  /// @return true if added, false if at capacity
  bool add_monitor(IMonitor &monitor) {
    if (count_ >= MAX_MONITORS) {
      return false;
    }
    monitor.set_data_handler(&data_manager_);
    monitors_.at(count_++) = &monitor;
    return true;
  }

  /// Get number of registered monitors
  [[nodiscard]] size_t monitor_count() const { return count_; }

  /// Find monitor by id (accepts any type convertible to SensorIdType)
  template <typename IdType> [[nodiscard]] IMonitor *find(IdType sensor_id) {
    auto target_id = static_cast<SensorIdType>(sensor_id);
    for (size_t i = 0; i < count_; ++i) {
      if (monitors_.at(i)->id() == target_id) {
        return monitors_.at(i);
      }
    }
    return nullptr;
  }

  /// Find monitor by id (const)
  template <typename IdType>
  [[nodiscard]] const IMonitor *find(IdType sensor_id) const {
    auto target_id = static_cast<SensorIdType>(sensor_id);
    for (size_t i = 0; i < count_; ++i) {
      if (monitors_.at(i)->id() == target_id) {
        return monitors_.at(i);
      }
    }
    return nullptr;
  }

  // ==========================================================================
  // Zero-allocation read methods (delegate to DataManager)
  // ==========================================================================

  /// Read into caller-provided buffer
  template <typename IdType>
  [[nodiscard]] size_t read_into(IdType sensor_id,
                                 std::span<Measurement> out) const {
    return data_manager_.read_into(static_cast<SensorIdType>(sensor_id), out);
  }

  /// Read all into caller-provided buffer
  [[nodiscard]] size_t read_all_into(std::span<Measurement> out) const {
    return data_manager_.read_all_into(out);
  }

  /// Visit each measurement
  template <typename Func>
  void for_each_measurement(const Func &callback) const {
    data_manager_.for_each(callback);
  }

  /// Get total measurement count
  [[nodiscard]] size_t total_measurement_count() const {
    return data_manager_.total_measurement_count();
  }

  /// Stop all monitors
  void stop_all() {
    for (size_t i = 0; i < count_; ++i) {
      monitors_.at(i)->stop();
    }
  }

  /// Start all monitors
  void start_all() {
    for (size_t i = 0; i < count_; ++i) {
      monitors_.at(i)->start();
    }
  }

  /// Iterate over all monitors
  template <typename Func> void for_each(const Func &callback) {
    for (size_t i = 0; i < count_; ++i) {
      callback(*monitors_.at(i));
    }
  }

  template <typename Func> void for_each(const Func &callback) const {
    for (size_t i = 0; i < count_; ++i) {
      callback(*monitors_.at(i));
    }
  }

  /// Get DataManager reference
  [[nodiscard]] DataManagerType &data_manager() { return data_manager_; }

private:
  std::array<IMonitor *, MAX_MONITORS> monitors_{};
  size_t count_ = 0;
  DataManagerType &data_manager_;
};

} // namespace sensor
