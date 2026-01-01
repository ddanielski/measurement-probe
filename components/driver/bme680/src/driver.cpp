/**
 * @file driver.cpp
 * @brief BME680 driver implementation using Bosch BME68x API (VFS-style)
 */

#include "bme680/driver.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <vector>

namespace driver::bme680 {

namespace {
constexpr const char *TAG = "bme680_drv";
} // namespace

BME680Driver::BME680Driver(i2c::IMaster &bus, uint8_t address)
    : device_(bus.create_device(address)) {

  std::memset(&dev_, 0, sizeof(dev_));
  dev_.intf = BME68X_I2C_INTF;
  dev_.intf_ptr = this;
  dev_.read = i2c_read;
  dev_.write = i2c_write;
  dev_.delay_us = delay_us;
  dev_.amb_temp = 25;
}

BME680Driver::~BME680Driver() {
  if (is_open_) {
    close();
  }
}

core::Status BME680Driver::open() {
  if (!device_ || !device_->valid()) {
    return ESP_ERR_INVALID_STATE;
  }

  int8_t rslt = bme68x_init(&dev_);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "bme68x_init failed: %d", rslt);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Chip ID: 0x%02X, Variant: %s", dev_.chip_id,
           dev_.variant_id == BME68X_VARIANT_GAS_HIGH ? "BME688" : "BME680");

  // Apply default configuration
  Config default_config{};
  auto status = configure_impl(default_config);
  if (!status.ok()) {
    return status;
  }

  is_open_ = true;
  return ESP_OK;
}

core::Status BME680Driver::close() {
  if (!is_open_) {
    return ESP_OK;
  }

  bme68x_set_op_mode(BME68X_SLEEP_MODE, &dev_);
  is_open_ = false;
  return ESP_OK;
}

core::Result<size_t> BME680Driver::read(std::span<uint8_t> buffer) {
  if (buffer.size() < sizeof(SensorData)) {
    return ESP_ERR_INVALID_SIZE;
  }

  // Self-managing: open if not already open
  if (!is_open_) {
    auto status = open();
    if (!status.ok()) {
      return status.error();
    }
  }

  auto result = read_data_impl();
  if (!result.ok()) {
    return result.error();
  }

  std::memcpy(buffer.data(), &result.value(), sizeof(SensorData));
  return sizeof(SensorData);
}

core::Result<size_t> BME680Driver::write(std::span<const uint8_t> data) {
  (void)data;
  return ESP_ERR_NOT_SUPPORTED;
}

core::Result<std::any> BME680Driver::ioctl(uint32_t cmd, std::any arg) {
  // Self-managing: open if not already open
  if (!is_open_) {
    auto status = open();
    if (!status.ok()) {
      return status.error();
    }
  }

  auto command = static_cast<IoctlCmd>(cmd);

  switch (command) {
  case IoctlCmd::Configure: {
    if (!arg.has_value()) {
      return ESP_ERR_INVALID_ARG;
    }
    const auto *config = std::any_cast<const Config *>(arg);
    if (config == nullptr) {
      return ESP_ERR_INVALID_ARG;
    }
    auto status = configure_impl(*config);
    return status.ok() ? core::Result<std::any>(std::any{})
                       : core::Result<std::any>(status.error());
  }

  case IoctlCmd::TriggerMeasurement: {
    auto status = trigger_measurement_impl();
    return status.ok() ? core::Result<std::any>(std::any{})
                       : core::Result<std::any>(status.error());
  }

  case IoctlCmd::GetMeasurementDuration: {
    auto duration = measurement_duration_impl();
    return std::any(duration);
  }

  case IoctlCmd::GetDeviceInfo: {
    DeviceInfo info{};
    info.chip_id = dev_.chip_id;
    info.variant_id = dev_.variant_id;
    return std::any(info);
  }

  case IoctlCmd::ReadData: {
    auto result = read_data_impl();
    if (!result.ok()) {
      return result.error();
    }
    return std::any(result.value());
  }

  default:
    return ESP_ERR_NOT_SUPPORTED;
  }
}
// Private implementations

core::Status BME680Driver::configure_impl(const Config &config) {
  conf_.os_temp = config.temp_os;
  conf_.os_pres = config.pres_os;
  conf_.os_hum = config.hum_os;
  conf_.filter = config.filter;
  conf_.odr = config.odr;

  int8_t rslt = bme68x_set_conf(&conf_, &dev_);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "bme68x_set_conf failed: %d", rslt);
    return ESP_FAIL;
  }

  if (config.enable_gas) {
    heatr_conf_.enable = BME68X_ENABLE;
    heatr_conf_.heatr_temp = config.heater_temp;
    heatr_conf_.heatr_dur = config.heater_dur;

    rslt = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf_, &dev_);
    if (rslt != BME68X_OK) {
      ESP_LOGE(TAG, "bme68x_set_heatr_conf failed: %d", rslt);
      return ESP_FAIL;
    }
  } else {
    heatr_conf_.enable = BME68X_DISABLE;
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf_, &dev_);
  }

  return ESP_OK;
}

core::Status BME680Driver::trigger_measurement_impl() {
  int8_t rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &dev_);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "bme68x_set_op_mode failed: %d", rslt);
    return ESP_FAIL;
  }
  return ESP_OK;
}

std::chrono::microseconds BME680Driver::measurement_duration_impl() {
  uint32_t dur_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf_, &dev_);
  dur_us += static_cast<uint32_t>(heatr_conf_.heatr_dur) * 1000;
  return std::chrono::microseconds(dur_us);
}

core::Result<SensorData> BME680Driver::read_data_impl() {
  // Trigger measurement
  auto status = trigger_measurement_impl();
  if (!status.ok()) {
    return status.error();
  }

  // Wait for measurement
  auto duration = measurement_duration_impl();
  uint32_t delay_ms = static_cast<uint32_t>(duration.count() / 1000) + 1;
  vTaskDelay(pdMS_TO_TICKS(delay_ms));

  // Read data
  bme68x_data data{};
  uint8_t n_fields = 0;

  int8_t rslt = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &dev_);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "bme68x_get_data failed: %d", rslt);
    return ESP_FAIL;
  }

  if (n_fields == 0) {
    return ESP_ERR_NOT_FOUND;
  }

  SensorData result{};
  result.temperature = data.temperature;
  result.pressure = data.pressure / 100.0F;
  result.humidity = data.humidity;
  result.gas_resistance = data.gas_resistance;
  result.gas_valid = (data.status & BME68X_GASM_VALID_MSK) != 0;
  result.heater_stable = (data.status & BME68X_HEAT_STAB_MSK) != 0;

  return result;
}

// Bosch API callbacks

BME68X_INTF_RET_TYPE BME680Driver::i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                            uint32_t length, void *intf_ptr) {
  auto *self = static_cast<BME680Driver *>(intf_ptr);
  if (self == nullptr || self->device_ == nullptr) {
    return BME68X_E_COM_FAIL;
  }

  std::array<uint8_t, 1> tx = {reg_addr};
  std::span<uint8_t> rx(reg_data, length);

  esp_err_t err = self->device_->write_read(tx, rx);
  return (err == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

BME68X_INTF_RET_TYPE BME680Driver::i2c_write(uint8_t reg_addr,
                                             const uint8_t *reg_data,
                                             uint32_t length, void *intf_ptr) {
  auto *self = static_cast<BME680Driver *>(intf_ptr);
  if (self == nullptr || self->device_ == nullptr) {
    return BME68X_E_COM_FAIL;
  }

  std::vector<uint8_t> buffer;
  buffer.reserve(length + 1);
  buffer.push_back(reg_addr);
  buffer.insert(buffer.end(), reg_data, reg_data + length);

  esp_err_t err = self->device_->write(buffer);
  return (err == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

void BME680Driver::delay_us(uint32_t period, void *intf_ptr) {
  (void)intf_ptr;
  uint32_t ticks = (period / 1000) / portTICK_PERIOD_MS;
  if (ticks == 0) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

} // namespace driver::bme680
