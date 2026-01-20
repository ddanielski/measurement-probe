/**
 * @file app.hpp
 * @brief Measurement probe application
 *
 * High-level application logic. Receives dependencies via constructor
 * (Board, Storage) - does not create them.
 */

#pragma once

#include "board.hpp"
#include "sensor_ids.hpp"
#include "timestamp_sensor.hpp"

#include <bme680/sensor.hpp>
#include <cloud/cloud_manager.hpp>
#include <core/app_events.hpp>
#include <core/application.hpp>
#include <core/event_loop.hpp>
#include <core/rtc_storage.hpp>
#include <core/timer.hpp>
#include <network/wifi_manager.hpp>
#include <power/sleep.hpp>
#include <sensor/data_manager.hpp>
#include <sensor/events.hpp>
#include <sensor/manager.hpp>
#include <sensor/monitor.hpp>

#include <atomic>
#include <memory>

namespace application {

// Application-specific type aliases using our SensorId registry
using DataManager = sensor::DataManagerT<sensor::sensor_type_count()>;
using SensorManager = sensor::SensorManagerT<DataManager>;

/// Measurement probe application
class MeasurementProbe final : public core::Application {
public:
  /// Construct with dependencies (does not take ownership)
  MeasurementProbe(Board &board, std::chrono::seconds sleep_interval);

protected:
  void run() override;

private:
  static constexpr const char *TAG = "probe";

  /// Maximum measurements buffer for zero-allocation logging
  static constexpr size_t MAX_MEASUREMENTS = 32;

  static void log_boot_info();
  void track_boot_count();
  void init_wifi();
  void init_sensors();
  void run_continuous_mode();

  /// Handle WiFi state changes
  void on_wifi_state_change(network::WifiState old_state,
                            network::WifiState new_state);

  /// Handle sensor data ready events (event-driven)
  void on_sensor_data(const sensor::SensorDataEvent &event);

  /// Static event handler (bridges ESP-IDF callback to member function)
  static void sensor_event_handler(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *event_data);

  /// Called periodically to log sensor readings (aggregated)
  void on_log_timer();

  /// Initialize cloud services
  void init_cloud();

  /// Start cloud when WiFi connects
  void start_cloud();

  /// Send telemetry to cloud
  void send_telemetry();

  /// Static cloud event handler (bridges ESP-IDF callback to member function)
  static void cloud_event_handler(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data);

  /// Static network event handler (sets flags for deferred execution)
  static void network_event_handler(void *arg, esp_event_base_t base,
                                    int32_t event_id, void *event_data);

  /// Handle factory reset using generic storage interface
  void handle_factory_reset();

  /// Handle device revocation
  void on_device_revoked();

  Board &board_;
  DataManager data_manager_;
  SensorManager sensors_{data_manager_};
  power::DeepSleep sleep_;
  network::WifiManager wifi_;

  /// Event subscriptions
  core::EventSubscription sensor_event_sub_;
  core::EventSubscription cloud_event_sub_;
  core::EventSubscription network_event_sub_;

  /// Deferred cloud operations (set by event, executed on main task)
  std::atomic<bool> cloud_start_pending_{false};
  std::atomic<bool> cloud_stop_pending_{false};
  std::atomic<bool> device_info_pending_{false};

  /// Periodic logging timer
  std::unique_ptr<core::PeriodicTimer> log_timer_;

  // Monitors (owned by app, registered with manager)
  // Using optional for deferred initialization
  using TimestampMonitor = sensor::SensorMonitor<TimestampSensor>;
  using BME680Monitor =
      sensor::ExternallyTimedMonitor<sensor::bme680::BME680Sensor>;

  std::optional<TimestampMonitor> timestamp_monitor_;
  std::optional<BME680Monitor> bme680_monitor_;

  /// Cloud connectivity (optional - device may not be provisioned)
  std::optional<cloud::CloudManager> cloud_;
};

} // namespace application
