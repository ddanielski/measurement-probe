/**
 * @file main.cpp
 * @brief Application entry point
 */

#include "probe_app.hpp"

#include <esp_log.h>

extern "C" void app_main() {
  MeasurementProbe app;
  if (auto err = app.start(); !err.ok()) {
    ESP_LOGE("main", "App failed: %s", esp_err_to_name(err.error()));
    // In production: could retry, enter recovery mode, etc.
  }
}
