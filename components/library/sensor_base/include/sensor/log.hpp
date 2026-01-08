/**
 * @file log.hpp
 * @brief Measurement logging utilities
 */

#pragma once

#include "measurement.hpp"

#include <esp_log.h>

#include <cinttypes>
#include <span>
#include <type_traits>

namespace sensor {

/// Log a batch of measurements
inline void log_measurements(const char *tag,
                             std::span<const Measurement> measurements) {
  if (measurements.empty()) {
    return;
  }

  ESP_LOGI(tag, "--- Sensor Readings (%zu) ---", measurements.size());

  for (const auto &m : measurements) {
    m.visit([&m, tag](auto &&v) {
      using T = std::decay_t<decltype(v)>;

      if constexpr (std::is_same_v<T, bool>) {
        ESP_LOGI(tag, "  %s: %s", m.name(), v ? "true" : "false");
      } else if constexpr (std::is_same_v<T, uint64_t>) {
        ESP_LOGI(tag, "  %s: %" PRIu64 " %s", m.name(), v, m.unit());
      } else if constexpr (std::is_same_v<T, int64_t>) {
        ESP_LOGI(tag, "  %s: %" PRId64 " %s", m.name(), v, m.unit());
      } else if constexpr (std::is_same_v<T, uint32_t>) {
        ESP_LOGI(tag, "  %s: %" PRIu32 " %s", m.name(), v, m.unit());
      } else if constexpr (std::is_same_v<T, int32_t>) {
        ESP_LOGI(tag, "  %s: %" PRId32 " %s", m.name(), v, m.unit());
      } else if constexpr (std::is_same_v<T, uint8_t>) {
        ESP_LOGI(tag, "  %s: %u %s", m.name(), static_cast<unsigned>(v),
                 m.unit());
      } else if constexpr (std::is_floating_point_v<T>) {
        ESP_LOGI(tag, "  %s: %.2f %s", m.name(), static_cast<double>(v),
                 m.unit());
      }
    });
  }
}

} // namespace sensor
