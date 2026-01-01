/**
 * @file board.cpp
 * @brief Board hardware initialization
 */

#include <application/board.hpp>

#include <esp_log.h>

namespace application {

namespace {
constexpr const char *TAG = "board";
} // namespace

Board::Board(const BoardConfig &config) {
  // Initialize I2C bus
  driver::i2c::Config i2c_config{
      .sda_pin = config.i2c_sda,
      .scl_pin = config.i2c_scl,
      .freq_hz = config.i2c_freq_hz,
  };

  i2c_ = std::make_unique<driver::i2c::Master>(i2c_config);

  if (!i2c_->valid()) {
    ESP_LOGE(TAG, "Failed to initialize I2C bus");
    return;
  }

  ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d, %luHz)",
           static_cast<int>(config.i2c_sda), static_cast<int>(config.i2c_scl),
           static_cast<unsigned long>(config.i2c_freq_hz));

  valid_ = true;
}

} // namespace application
