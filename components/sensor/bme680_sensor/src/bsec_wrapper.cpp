/**
 * @file bsec_wrapper.cpp
 * @brief BSEC2 wrapper implementation
 */

#include "bme680/bsec_wrapper.hpp"

#include <bsec_config.h>

#include <esp_log.h>
#include <esp_timer.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace sensor::bme680 {

namespace {
constexpr const char *TAG = "bsec";
} // namespace

core::Status BsecWrapper::init() {
  bsec_library_return_t rslt = bsec_init();
  if (rslt != BSEC_OK) {
    ESP_LOGE(TAG, "bsec_init failed: %d", rslt);
    return ESP_FAIL;
  }

  // Get version
  bsec_version_t ver{};
  bsec_get_version(&ver);
  snprintf(version_str_.data(), version_str_.size(), "%d.%d.%d.%d", ver.major,
           ver.minor, ver.major_bugfix, ver.minor_bugfix);

  ESP_LOGI(TAG, "BSEC v%s initialized", version_str_.data());

  // Load BME680 IAQ configuration (required for LP mode)
  rslt = bsec_set_configuration(&bsec_config_iaq[0], bsec_config_iaq_len,
                                work_buffer_.data(), work_buffer_.size());
  if (rslt != BSEC_OK) {
    ESP_LOGE(TAG, "bsec_set_configuration failed: %d", rslt);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "BSEC config loaded (LP mode, 3s)");

  initialized_ = true;
  return ESP_OK;
}

core::Status BsecWrapper::subscribe_all() {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  // Use sample rate from generated config (LP = 3s, ULP = 300s)
  sample_interval_ = std::chrono::milliseconds(BSEC_CONFIGURED_INTERVAL_MS);

  // Request all standard outputs at configured sample rate
  constexpr std::array OUTPUT_IDS = std::to_array<uint8_t>({
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_GAS,
  });

  std::array<bsec_sensor_configuration_t, OUTPUT_IDS.size()> requested{};
  for (size_t i = 0; i < OUTPUT_IDS.size(); ++i) {
    requested.at(i).sample_rate = BSEC_CONFIGURED_SAMPLE_RATE;
    requested.at(i).sensor_id = OUTPUT_IDS.at(i);
  }

  std::array<bsec_sensor_configuration_t, BSEC_MAX_PHYSICAL_SENSOR> required{};
  uint8_t n_required = required.size();

  bsec_library_return_t rslt = bsec_update_subscription(
      requested.data(), requested.size(), required.data(), &n_required);

  if (rslt < BSEC_OK) { // Only negative values are errors
    ESP_LOGE(TAG, "bsec_update_subscription failed: %d", rslt);
    return ESP_FAIL;
  }
  if (rslt > BSEC_OK) {
    ESP_LOGW(TAG, "bsec_update_subscription warning: %d", rslt);
  }

  ESP_LOGI(TAG, "Subscribed to %zu outputs, requires %d physical sensors",
           requested.size(), n_required);
  return ESP_OK;
}

BsecSensorSettings BsecWrapper::get_sensor_settings(int64_t time_ns) const {
  BsecSensorSettings result{};

  if (!initialized_) {
    return result;
  }

  bsec_bme_settings_t settings{};
  bsec_library_return_t rslt = bsec_sensor_control(time_ns, &settings);

  if (rslt != BSEC_OK) {
    ESP_LOGW(TAG, "bsec_sensor_control failed: %d", rslt);
    return result;
  }

  result.next_call_time_ns = settings.next_call;
  result.process_data = settings.process_data;
  result.heater_temperature = settings.heater_temperature;
  result.heater_duration = settings.heater_duration;
  result.run_gas = settings.run_gas;
  result.temperature_oversampling = settings.temperature_oversampling;
  result.pressure_oversampling = settings.pressure_oversampling;
  result.humidity_oversampling = settings.humidity_oversampling;

  return result;
}

core::Result<BsecOutput> BsecWrapper::process(int64_t time_ns,
                                              float temperature, float pressure,
                                              float humidity,
                                              float gas_resistance,
                                              bool gas_valid) const {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  // Build BSEC inputs
  enum class In : uint8_t { Temperature, Humidity, Pressure, Gas, Count };
  auto I = [](In i) { return static_cast<size_t>(i); };

  auto make_input = [time_ns](uint8_t id, float signal) {
    bsec_input_t input{};
    input.sensor_id = id;
    input.signal = signal;
    input.time_stamp = time_ns;
    return input;
  };

  std::array<bsec_input_t, I(In::Count)> inputs{};
  inputs.at(I(In::Temperature)) =
      make_input(BSEC_INPUT_TEMPERATURE, temperature);
  inputs.at(I(In::Humidity)) = make_input(BSEC_INPUT_HUMIDITY, humidity);
  inputs.at(I(In::Pressure)) = make_input(BSEC_INPUT_PRESSURE, pressure);

  auto n_inputs = static_cast<uint8_t>(I(In::Gas)); // 3 inputs without gas

  if (gas_valid) {
    inputs.at(I(In::Gas)) = make_input(BSEC_INPUT_GASRESISTOR, gas_resistance);
    n_inputs = static_cast<uint8_t>(I(In::Count)); // 4 inputs with gas
  }

  // Process through BSEC
  std::array<bsec_output_t, BSEC_NUMBER_OUTPUTS> outputs{};
  uint8_t n_outputs = outputs.size();

  bsec_library_return_t rslt =
      bsec_do_steps(inputs.data(), n_inputs, outputs.data(), &n_outputs);

  if (rslt != BSEC_OK) {
    ESP_LOGE(TAG, "bsec_do_steps failed: %d", rslt);
    return ESP_FAIL;
  }

  // Parse outputs
  BsecOutput result{};
  result.valid = false;

  for (uint8_t i = 0; i < n_outputs; i++) {
    const auto &out = outputs.at(i);
    switch (out.sensor_id) {
    case BSEC_OUTPUT_IAQ:
      result.iaq = out.signal;
      result.iaq_accuracy = out.accuracy;
      result.valid = true;
      break;
    case BSEC_OUTPUT_STATIC_IAQ:
      result.static_iaq = out.signal;
      break;
    case BSEC_OUTPUT_CO2_EQUIVALENT:
      result.co2 = out.signal;
      break;
    case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
      result.voc = out.signal;
      break;
    case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
      result.temperature = out.signal;
      break;
    case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
      result.humidity = out.signal;
      break;
    case BSEC_OUTPUT_RAW_PRESSURE:
      result.pressure = out.signal / 100.0F; // Pa to hPa
      break;
    case BSEC_OUTPUT_RAW_GAS:
      result.gas_resistance = out.signal;
      break;
    default:
      break;
    }
  }

  return result;
}

const char *BsecWrapper::version() const { return version_str_.data(); }

core::Status BsecWrapper::save_state(core::IStorage &storage) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  std::array<uint8_t, STATE_SIZE> state{};
  uint32_t n_state = 0;

  bsec_library_return_t rslt =
      bsec_get_state(0, state.data(), state.size(), work_buffer_.data(),
                     work_buffer_.size(), &n_state);

  if (rslt != BSEC_OK) {
    ESP_LOGE(TAG, "bsec_get_state failed: %d", rslt);
    return ESP_FAIL;
  }

  // Store as blob
  auto result = storage.set_blob(STATE_KEY, std::span{state.data(), n_state});
  if (result.ok()) {
    ESP_LOGI(TAG, "Saved BSEC state (%lu bytes)",
             static_cast<unsigned long>(n_state));
  }
  return result;
}

core::Status BsecWrapper::load_state(core::IStorage &storage) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  // Get blob size first
  auto size_result = storage.get_blob_size(STATE_KEY);
  if (!size_result.ok()) {
    ESP_LOGI(TAG, "No saved BSEC state found");
    return ESP_OK; // Not an error, just no saved state
  }

  size_t n_state = size_result.value();
  if (n_state > STATE_SIZE) {
    ESP_LOGW(TAG, "BSEC state too large: %zu", n_state);
    return ESP_FAIL;
  }

  std::array<uint8_t, STATE_SIZE> state{};
  auto status = storage.get_blob(STATE_KEY, std::span{state.data(), n_state});
  if (!status.ok()) {
    ESP_LOGW(TAG, "Failed to read BSEC state");
    return status;
  }

  bsec_library_return_t rslt = bsec_set_state(
      state.data(), n_state, work_buffer_.data(), work_buffer_.size());

  if (rslt != BSEC_OK) {
    ESP_LOGW(TAG, "bsec_set_state failed: %d (state may be stale)", rslt);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Loaded BSEC state (%zu bytes)", n_state);
  return ESP_OK;
}

} // namespace sensor::bme680
