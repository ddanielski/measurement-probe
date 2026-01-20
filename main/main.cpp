/**
 * @file main.cpp
 * @brief Application entry point
 *
 * Creates and wires up all dependencies, then starts the application.
 */

#include "app_config.hpp"

#include <application/app.hpp>
#include <application/board.hpp>

#include <esp_log.h>

#include <memory>

namespace {
constexpr const char *TAG = "main";
} // namespace

extern "C" void app_main() {
  // Create board with configuration
  application::BoardConfig board_config{
      .i2c_sda = app::config::I2C_SDA_PIN,
      .i2c_scl = app::config::I2C_SCL_PIN,
  };

  // Board on stack is small
  application::Board board(board_config);
  if (!board.valid()) {
    ESP_LOGE(TAG, "Board initialization failed");
    return;
  }

  // Application is too large for stack - allocate on heap
  auto app = std::make_unique<application::MeasurementProbe>(
      board, std::chrono::seconds(app::config::SLEEP_INTERVAL_SEC));

  if (auto err = app->start(); !err) {
    ESP_LOGE(TAG, "App failed: %s", esp_err_to_name(err.error()));
  }
}
