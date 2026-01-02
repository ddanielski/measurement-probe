/**
 * @file manager.hpp
 * @brief Sensor manager for registration and coordinated reading
 */

#pragma once

#include "sensor.hpp"

#include <core/result.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace sensor {

/// Manages sensor registration and coordinated operations
class SensorManager {
public:
  SensorManager() = default;
  ~SensorManager() = default;

  SensorManager(const SensorManager &) = delete;
  SensorManager &operator=(const SensorManager &) = delete;
  SensorManager(SensorManager &&) = default;
  SensorManager &operator=(SensorManager &&) = default;

  /// Register a sensor (takes ownership)
  void register_sensor(std::unique_ptr<ISensor> sensor) {
    if (sensor) {
      sensors_.push_back(std::move(sensor));
    }
  }

  /// Get number of registered sensors
  [[nodiscard]] size_t sensor_count() const { return sensors_.size(); }

  /// Find sensor by name
  [[nodiscard]] ISensor *find(std::string_view name) {
    for (auto &s : sensors_) {
      if (s->name() == name) {
        return s.get();
      }
    }
    return nullptr;
  }

  /// Read from a specific sensor by name
  [[nodiscard]] core::Result<std::span<const Measurement>>
  read(std::string_view name) {
    auto *sensor = find(name);
    if (sensor == nullptr) {
      return core::Err(ESP_ERR_NOT_FOUND);
    }
    return sensor->read();
  }

  /// Read all sensors, returns flattened measurements
  [[nodiscard]] core::Result<std::vector<Measurement>> read_all() {
    std::vector<Measurement> all;

    // Reserve approximate size
    size_t total = 0;
    for (const auto &s : sensors_) {
      total += s->measurement_count();
    }
    all.reserve(total);

    // Read each sensor
    for (auto &s : sensors_) {
      auto result = s->read();
      if (!result) {
        // Continue reading other sensors on failure
        continue;
      }
      for (const auto &m : *result) {
        all.push_back(m);
      }
    }

    return all;
  }

  /// Put all sensors to sleep
  void sleep_all() {
    for (auto &s : sensors_) {
      (void)s->sleep();
    }
  }

  /// Wake all sensors
  void wake_all() {
    for (auto &s : sensors_) {
      (void)s->wake();
    }
  }

  /// Iterate over all sensors
  template <typename Fn> void for_each(Fn &&fn) {
    for (auto &s : sensors_) {
      fn(*s);
    }
  }

  /// Get required sample interval (max of all sensor min_intervals)
  [[nodiscard]] std::chrono::milliseconds sample_interval() {
    if (sensors_.empty()) {
      return std::chrono::milliseconds(1000); // Default 1s
    }

    auto max_interval = sensors_.front()->min_interval();
    for (auto &s : sensors_) {
      auto interval = s->min_interval();
      if (interval > max_interval) {
        max_interval = interval;
      }
    }
    return max_interval;
  }

private:
  std::vector<std::unique_ptr<ISensor>> sensors_;
};

} // namespace sensor
