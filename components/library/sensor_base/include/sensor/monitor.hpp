/**
 * @file monitor.hpp
 * @brief Sensor monitors - coordinate timing and data flow
 *
 * Monitors own sensors and manage:
 * - When to sample (timing)
 * - Where to route data (via IDataHandler)
 *
 * Two types:
 * - SensorMonitor: Fixed interval, user-configurable
 * - ExternallyTimedMonitor: Sensor controls timing (e.g., BSEC)
 */

#pragma once

#include "data_manager.hpp"
#include "sensor.hpp"

#include <core/timer.hpp>

#include <chrono>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sensor {

// ============================================================================
// IMonitor Interface - What SensorManager works with
// ============================================================================

/// Interface for sensor monitors
/// SensorManager holds these polymorphically
class IMonitor {
public:
  virtual ~IMonitor() = default;

  IMonitor(const IMonitor &) = delete;
  IMonitor &operator=(const IMonitor &) = delete;
  IMonitor(IMonitor &&) = delete;
  IMonitor &operator=(IMonitor &&) = delete;

  /// Sensor type identifier (raw uint8_t, from application's SensorId)
  [[nodiscard]] virtual SensorIdType id() const = 0;

  /// Human-readable sensor name
  [[nodiscard]] virtual std::string_view name() const = 0;

  /// Number of measurements
  [[nodiscard]] virtual size_t measurement_count() const = 0;

  /// Stop sampling
  virtual void stop() = 0;

  /// Resume sampling
  virtual void start() = 0;

  /// Check if monitor is running
  [[nodiscard]] virtual bool is_running() const = 0;

  /// Set data handler for routing measurements to DataManager
  virtual void set_data_handler(IDataHandler *handler) = 0;

  /// Consecutive error count (resets on successful sample)
  [[nodiscard]] virtual uint32_t error_count() const = 0;

protected:
  IMonitor() = default;
};

// ============================================================================
// SensorMonitor - Fixed interval sampling
// ============================================================================

/// Monitor that samples a sensor at a fixed interval
template <typename Sensor> class SensorMonitor final : public IMonitor {
  static_assert(std::is_base_of_v<ISensor, Sensor>,
                "Sensor must implement ISensor");

public:
  /// Construct with interval and sensor constructor arguments
  template <typename... Args>
  explicit SensorMonitor(std::chrono::milliseconds interval, Args &&...args)
      : sensor_(std::forward<Args>(args)...), interval_(interval),
        timer_([this]() { on_timer(); }) {}

  [[nodiscard]] SensorIdType id() const override { return sensor_.id(); }

  [[nodiscard]] std::string_view name() const override {
    return sensor_.name();
  }

  [[nodiscard]] size_t measurement_count() const override {
    return sensor_.measurement_count();
  }

  void stop() override {
    running_ = false;
    [[maybe_unused]] auto status = timer_.stop();
  }

  void start() override {
    if (running_) {
      return;
    }
    running_ = true;
    // Sample immediately on start
    do_sample();
    schedule_next();
  }

  [[nodiscard]] bool is_running() const override { return running_; }

  void set_data_handler(IDataHandler *handler) override {
    data_handler_ = handler;
  }

  [[nodiscard]] uint32_t error_count() const override {
    return consecutive_errors_;
  }

  /// Get the configured interval
  [[nodiscard]] std::chrono::milliseconds interval() const { return interval_; }

  /// Access underlying sensor (for sensor-specific operations)
  [[nodiscard]] Sensor &sensor() { return sensor_; }
  [[nodiscard]] const Sensor &sensor() const { return sensor_; }

private:
  void on_timer() {
    do_sample();
    schedule_next();
  }

  void do_sample() {
    auto measurements = sensor_.sample();

    if (measurements.empty()) {
      consecutive_errors_++;
      return;
    }

    consecutive_errors_ = 0;

    if (data_handler_ != nullptr) {
      data_handler_->on_data(sensor_.id(), measurements);
    }
  }

  void schedule_next() {
    if (running_) {
      [[maybe_unused]] auto status = timer_.start(interval_);
    }
  }

  Sensor sensor_;
  std::chrono::milliseconds interval_;
  core::OneShotTimer timer_;
  IDataHandler *data_handler_ = nullptr;
  uint32_t consecutive_errors_ = 0;
  bool running_ = false;
};

// ============================================================================
// ExternallyTimedMonitor - Sensor controls timing
// ============================================================================

/// Monitor for sensors that control their own timing (e.g., BSEC)
/// Sensor must implement IExternallyTimedSensor
template <typename Sensor>
class ExternallyTimedMonitor final : public IMonitor {
  static_assert(std::is_base_of_v<IExternallyTimedSensor, Sensor>,
                "Sensor must implement IExternallyTimedSensor");

public:
  /// Construct with sensor constructor arguments
  template <typename... Args>
  explicit ExternallyTimedMonitor(Args &&...args)
      : sensor_(std::forward<Args>(args)...), timer_([this]() { on_timer(); }) {
  }

  [[nodiscard]] SensorIdType id() const override { return sensor_.id(); }

  [[nodiscard]] std::string_view name() const override {
    return sensor_.name();
  }

  [[nodiscard]] size_t measurement_count() const override {
    return sensor_.measurement_count();
  }

  void stop() override {
    running_ = false;
    [[maybe_unused]] auto status = timer_.stop();
  }

  void start() override {
    if (running_) {
      return;
    }
    running_ = true;
    schedule_next();
  }

  [[nodiscard]] bool is_running() const override { return running_; }

  void set_data_handler(IDataHandler *handler) override {
    data_handler_ = handler;
  }

  [[nodiscard]] uint32_t error_count() const override {
    return consecutive_errors_;
  }

  /// Access underlying sensor
  [[nodiscard]] Sensor &sensor() { return sensor_; }
  [[nodiscard]] const Sensor &sensor() const { return sensor_; }

private:
  void on_timer() {
    do_sample();
    schedule_next();
  }

  void do_sample() {
    auto measurements = sensor_.sample();

    if (measurements.empty()) {
      consecutive_errors_++;
      return;
    }

    consecutive_errors_ = 0;

    if (data_handler_ != nullptr) {
      data_handler_->on_data(sensor_.id(), measurements);
    }
  }

  void schedule_next() {
    if (!running_) {
      return;
    }
    auto delay = sensor_.next_sample_delay();
    // Minimum 10ms to avoid busy-looping
    if (delay < std::chrono::microseconds(10000)) {
      delay = std::chrono::microseconds(10000);
    }
    [[maybe_unused]] auto status = timer_.start(delay);
  }

  Sensor sensor_;
  core::OneShotTimer timer_;
  IDataHandler *data_handler_ = nullptr;
  uint32_t consecutive_errors_ = 0;
  bool running_ = false;
};

} // namespace sensor
