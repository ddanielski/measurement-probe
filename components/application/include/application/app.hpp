/**
 * @file app.hpp
 * @brief Measurement probe application
 *
 * High-level application logic. Receives dependencies via constructor
 * (Board, Storage) - does not create them.
 */

#pragma once

#include "board.hpp"

#include <core/application.hpp>
#include <power/sleep.hpp>
#include <sensor/manager.hpp>

namespace application {

/// Measurement probe application
class MeasurementProbe final : public core::Application {
public:
  /// Construct with dependencies (does not take ownership)
  MeasurementProbe(Board &board, std::chrono::seconds sleep_interval);

protected:
  void run() override;

private:
  static constexpr const char *TAG = "probe";

  static void log_boot_info();
  void track_boot_count();
  void init_sensors();
  void read_sensors();
  void run_continuous_mode();

  Board &board_;
  sensor::SensorManager sensors_;
  power::DeepSleep sleep_;
};

} // namespace application
