/**
 * @file wifi_manager.cpp
 * @brief WiFi manager implementation
 */

#include "network/wifi_manager.hpp"

#include <esp_log.h>
#include <esp_wifi.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include <algorithm>
#include <array>
#include <cstring>

// Define the network events base
CORE_EVENT_DEFINE_BASE(NETWORK_EVENTS);

namespace network {

namespace {
constexpr const char *TAG = "wifi_mgr";

/// Helper to publish network events
void publish_event(NetworkEvent event, const void *data = nullptr,
                   size_t data_size = 0) {
  esp_event_post(NETWORK_EVENTS, static_cast<int32_t>(event), data, data_size,
                 0);
}

template <typename T> void publish_event(NetworkEvent event, const T *data) {
  publish_event(event, data, sizeof(T));
}

} // namespace

// Static instance pointer for event handlers
WifiManager *WifiManager::instance_ = nullptr;

WifiManager::~WifiManager() {
  if (initialized_) {
    (void)disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (netif_ != nullptr) {
      esp_netif_destroy(netif_);
    }
    instance_ = nullptr;
  }
}

core::Status WifiManager::init(core::IStorage &storage,
                               const WifiConfig &config) {
  if (initialized_) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  storage_ = &storage;
  config_ = config;
  instance_ = this;

  // Initialize TCP/IP stack (safe to call multiple times)
  ESP_ERROR_CHECK(esp_netif_init());

  // Create default WiFi station
  netif_ = esp_netif_create_default_wifi_sta();
  if (netif_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create netif");
    return core::Err(ESP_ERR_NO_MEM);
  }

  // Initialize WiFi with default config
  wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
  if (auto err = esp_wifi_init(&wifi_init); err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
    return core::Err(err);
  }

  // Set storage mode to RAM (we manage credentials ourselves)
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  // Set station mode
  if (auto err = esp_wifi_set_mode(WIFI_MODE_STA); err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
    return core::Err(err);
  }

  // Subscribe to WiFi events
  wifi_sub_ = core::events().subscribe(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                       wifi_event_handler, this);

  // Subscribe to IP events
  ip_sub_ = core::events().subscribe(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     ip_event_handler, this);

  // Start WiFi
  if (auto err = esp_wifi_start(); err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
    return core::Err(err);
  }

  initialized_ = true;
  set_state(WifiState::Disconnected);

  ESP_LOGI(TAG, "WiFi manager initialized");
  return core::Ok();
}

core::Status WifiManager::connect() {
  if (!initialized_) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  auto creds = load_credentials();
  if (!creds) {
    ESP_LOGW(TAG, "No stored credentials");
    return core::Err(ESP_ERR_NOT_FOUND);
  }

  return start_connect(*creds);
}

core::Status WifiManager::connect(const WifiCredentials &creds) {
  if (!initialized_) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  if (!creds.is_valid()) {
    return core::Err(ESP_ERR_INVALID_ARG);
  }

  // Save credentials first
  if (auto err = save_credentials(creds); !err) {
    ESP_LOGW(TAG, "Failed to save credentials: %s",
             esp_err_to_name(err.error()));
    // Continue anyway - try to connect
  }

  return start_connect(creds);
}

core::Status WifiManager::disconnect() {
  if (!initialized_) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  if (state_ == WifiState::Provisioning) {
    (void)stop_provisioning();
  }

  reset_retry_state();

  if (auto err = esp_wifi_disconnect();
      err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
    return core::Err(err);
  }

  set_state(WifiState::Disconnected);
  return core::Ok();
}

core::Status WifiManager::start_provisioning(const ProvisioningConfig &config) {
  if (!initialized_) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  // Enforce PoP for security - never allow unsecured provisioning
  if (config.pop == nullptr || config.pop[0] == '\0') {
    ESP_LOGE(TAG, "PoP required for secure provisioning");
    return core::Err(ESP_ERR_INVALID_ARG);
  }

  if (state_ == WifiState::Connected) {
    ESP_LOGW(TAG, "Already connected, disconnecting for provisioning");
    (void)disconnect();
  }

  // Configure provisioning manager
  wifi_prov_mgr_config_t prov_config{};
  prov_config.scheme = wifi_prov_scheme_ble;
  // Use FREE_BLE for NimBLE stack (FREE_BTDM is for Bluedroid)
  prov_config.scheme_event_handler =
      WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE;
  prov_config.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;

  if (auto err = wifi_prov_mgr_init(prov_config); err != ESP_OK) {
    ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %s", esp_err_to_name(err));
    return core::Err(err);
  }

  // Subscribe to provisioning events
  prov_sub_ = core::events().subscribe(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                       prov_event_handler, this);

  // Security 1: Curve25519 key exchange + AES-CTR encryption with PoP
  constexpr wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
  const void *sec_params = static_cast<const void *>(config.pop);
  ESP_LOGI(TAG, "Using security 1 with PoP");

  // Generate device name with MAC suffix
  std::array<uint8_t, 6> mac{};
  esp_wifi_get_mac(WIFI_IF_STA, mac.data());

  std::array<char, 32> device_name{};
  snprintf(device_name.data(), device_name.size(), "%s_%02X%02X%02X",
           config.device_name_prefix != nullptr ? config.device_name_prefix
                                                : "PROV",
           mac[3], mac[4], mac[5]);

  // Configure custom service UUID if provided
  if (config.service_uuid != nullptr) {
    // Note: ESP-IDF requires non-const pointer for this API
    wifi_prov_scheme_ble_set_service_uuid(const_cast<uint8_t *>(
        reinterpret_cast<const uint8_t *>(config.service_uuid)));
  }

  ESP_LOGI(TAG, "Starting provisioning as '%s' (timeout: %us)",
           device_name.data(), config.timeout_sec);

  if (auto err = wifi_prov_mgr_start_provisioning(security, sec_params,
                                                  device_name.data(), nullptr);
      err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(err));
    wifi_prov_mgr_deinit();
    return core::Err(err);
  }

  // Start provisioning timeout timer
  if (config.timeout_sec > 0) {
    if (!prov_timeout_timer_) {
      prov_timeout_timer_ =
          std::make_unique<core::OneShotTimer>([this]() { on_prov_timeout(); });
    }
    (void)prov_timeout_timer_->stop();
    (void)prov_timeout_timer_->start(std::chrono::seconds(config.timeout_sec));
  }

  set_state(WifiState::Provisioning);
  publish_event(NetworkEvent::ProvisioningStarted);

  return core::Ok();
}

core::Status WifiManager::stop_provisioning() {
  if (state_ != WifiState::Provisioning) {
    return core::Ok();
  }

  // Stop timeout timer
  if (prov_timeout_timer_) {
    (void)prov_timeout_timer_->stop();
  }

  wifi_prov_mgr_stop_provisioning();
  wifi_prov_mgr_deinit();
  prov_sub_.unsubscribe();

  set_state(WifiState::Disconnected);

  return core::Ok();
}

bool WifiManager::has_credentials() const {
  if (storage_ == nullptr) {
    return false;
  }
  return storage_->contains(kKeySsid);
}

core::Status WifiManager::clear_credentials() {
  if (storage_ == nullptr) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  auto guard = storage_->auto_commit();
  (void)storage_->erase(kKeySsid);
  (void)storage_->erase(kKeyPassword);

  ESP_LOGI(TAG, "Credentials cleared");
  return core::Ok();
}

core::Result<WifiCredentials> WifiManager::load_credentials() {
  if (storage_ == nullptr) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  WifiCredentials creds{};

  // Load SSID
  auto ssid_size = storage_->get_string_size(kKeySsid);
  if (!ssid_size || *ssid_size == 0) {
    return core::Err(ESP_ERR_NOT_FOUND);
  }

  if (auto err = storage_->get_string(kKeySsid,
                                      {creds.ssid.data(), creds.ssid.size()});
      !err) {
    return core::Err(err.error());
  }

  // Load password (optional for open networks)
  auto pass_size = storage_->get_string_size(kKeyPassword);
  if (pass_size && *pass_size > 0) {
    (void)storage_->get_string(kKeyPassword,
                               {creds.password.data(), creds.password.size()});
  }

  ESP_LOGI(TAG, "Loaded credentials for '%s'", creds.ssid.data());
  return creds;
}

core::Status WifiManager::save_credentials(const WifiCredentials &creds) {
  if (storage_ == nullptr) {
    return core::Err(ESP_ERR_INVALID_STATE);
  }

  auto guard = storage_->auto_commit();

  if (auto err = storage_->set_string(kKeySsid, creds.ssid.data()); !err) {
    return err;
  }

  if (creds.password[0] != '\0') {
    if (auto err = storage_->set_string(kKeyPassword, creds.password.data());
        !err) {
      return err;
    }
  } else {
    (void)storage_->erase(kKeyPassword);
  }

  ESP_LOGI(TAG, "Saved credentials for '%s'", creds.ssid.data());
  return core::Ok();
}

core::Status WifiManager::start_connect(const WifiCredentials &creds) {
  // Store credentials for retry attempts
  pending_creds_ = creds;

  // Configure WiFi
  wifi_config_t wifi_config{};
  std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
               creds.ssid.data(), sizeof(wifi_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
               creds.password.data(), sizeof(wifi_config.sta.password) - 1);

  // Use WPA2/WPA3 if password provided
  if (creds.password[0] != '\0') {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  }

  wifi_config.sta.pmf_cfg.capable = true;

  if (auto err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
      err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
    return core::Err(err);
  }

  ESP_LOGI(TAG, "Connecting to '%s'...", creds.ssid.data());
  set_state(WifiState::Connecting);

  if (auto err = esp_wifi_connect(); err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    set_state(WifiState::Failed);
    return core::Err(err);
  }

  return core::Ok();
}

uint32_t WifiManager::calculate_backoff() const {
  // Exponential backoff: initial * 2^retry, capped at max
  uint32_t backoff = config_.initial_backoff_ms;
  for (uint8_t i = 0; i < retry_count_ && backoff < config_.max_backoff_ms;
       ++i) {
    backoff *= WifiConfig::kBackoffMultiplier;
  }
  return std::min(backoff, config_.max_backoff_ms);
}

void WifiManager::reset_retry_state() {
  retry_count_ = 0;
  if (reconnect_timer_) {
    (void)reconnect_timer_->stop();
  }
}

void WifiManager::schedule_reconnect(uint32_t backoff_ms) {
  if (!pending_creds_.is_valid()) {
    return;
  }

  if (config_.max_retries != 0 && retry_count_ >= config_.max_retries) {
    ESP_LOGE(TAG, "Max retries reached");
    set_state(WifiState::Failed);
    publish_event(NetworkEvent::WifiConnectionFailed);
    return;
  }

  uint32_t backoff = backoff_ms;
  ESP_LOGI(TAG, "Reconnecting in %lu ms (attempt %d)",
           static_cast<unsigned long>(backoff), retry_count_ + 1);
  retry_count_++;

  // Initialize timer lazily (needs 'this' pointer)
  if (!reconnect_timer_) {
    reconnect_timer_ = std::make_unique<core::OneShotTimer>(
        [this]() { on_reconnect_timer(); });
  }

  (void)reconnect_timer_->stop();
  (void)reconnect_timer_->start(std::chrono::milliseconds(backoff));
}

void WifiManager::on_reconnect_timer() {
  if (state_ == WifiState::Disconnected && pending_creds_.is_valid()) {
    set_state(WifiState::Connecting);
    esp_wifi_connect();
  }
}

void WifiManager::on_prov_timeout() {
  ESP_LOGW(TAG, "Provisioning timeout - stopping");
  publish_event(NetworkEvent::ProvisioningTimeout);
  (void)stop_provisioning();
  set_state(WifiState::Failed);
  publish_event(NetworkEvent::ProvisioningFailed);
}

void WifiManager::set_state(WifiState new_state) {
  WifiState old = state_.exchange(new_state);
  if (old != new_state) {
    ESP_LOGI(TAG, "State: %d -> %d", static_cast<int>(old),
             static_cast<int>(new_state));
    if (state_callback_) {
      state_callback_(old, new_state);
    }
  }
}

void WifiManager::wifi_event_handler(void *arg, esp_event_base_t /*base*/,
                                     int32_t event_id, void *event_data) {
  auto *self = static_cast<WifiManager *>(arg);

  switch (event_id) {
  case WIFI_EVENT_STA_START:
    ESP_LOGD(TAG, "WIFI_EVENT_STA_START");
    break;

  case WIFI_EVENT_STA_CONNECTED:
    ESP_LOGI(TAG, "Connected to AP");
    break;

  case WIFI_EVENT_STA_DISCONNECTED: {
    auto *info = static_cast<wifi_event_sta_disconnected_t *>(event_data);
    ESP_LOGW(TAG, "Disconnected from AP, reason: %d", info->reason);

    if (self->state_ == WifiState::Provisioning) {
      // Don't reconnect during provisioning
      break;
    }

    self->conn_info_ = {};
    self->set_state(WifiState::Disconnected);
    publish_event(NetworkEvent::WifiDisconnected);

    // Schedule non-blocking reconnect with backoff
    uint32_t backoff = self->calculate_backoff();
    self->schedule_reconnect(backoff);
    break;
  }

  default:
    break;
  }
}

void WifiManager::ip_event_handler(void *arg, esp_event_base_t /*base*/,
                                   int32_t event_id, void *event_data) {
  auto *self = static_cast<WifiManager *>(arg);

  if (event_id == IP_EVENT_STA_GOT_IP) {
    auto *info = static_cast<ip_event_got_ip_t *>(event_data);
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&info->ip_info.ip));

    // Store connection info
    self->conn_info_.ip[0] = esp_ip4_addr1(&info->ip_info.ip);
    self->conn_info_.ip[1] = esp_ip4_addr2(&info->ip_info.ip);
    self->conn_info_.ip[2] = esp_ip4_addr3(&info->ip_info.ip);
    self->conn_info_.ip[3] = esp_ip4_addr4(&info->ip_info.ip);

    self->conn_info_.gateway[0] = esp_ip4_addr1(&info->ip_info.gw);
    self->conn_info_.gateway[1] = esp_ip4_addr2(&info->ip_info.gw);
    self->conn_info_.gateway[2] = esp_ip4_addr3(&info->ip_info.gw);
    self->conn_info_.gateway[3] = esp_ip4_addr4(&info->ip_info.gw);

    self->conn_info_.netmask[0] = esp_ip4_addr1(&info->ip_info.netmask);
    self->conn_info_.netmask[1] = esp_ip4_addr2(&info->ip_info.netmask);
    self->conn_info_.netmask[2] = esp_ip4_addr3(&info->ip_info.netmask);
    self->conn_info_.netmask[3] = esp_ip4_addr4(&info->ip_info.netmask);

    // Get RSSI
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      self->conn_info_.rssi = ap_info.rssi;
      self->conn_info_.channel = ap_info.primary;
    }

    self->reset_retry_state();
    self->set_state(WifiState::Connected);

    // Publish connected event with connection info
    publish_event(NetworkEvent::WifiConnected, &self->conn_info_);
  }
}

void WifiManager::prov_event_handler(void *arg, esp_event_base_t /*base*/,
                                     int32_t event_id, void *event_data) {
  auto *self = static_cast<WifiManager *>(arg);

  switch (event_id) {
  case WIFI_PROV_START:
    ESP_LOGI(TAG, "Provisioning started - awaiting BLE connection");
    publish_event(NetworkEvent::BlePairingStarted);
    break;

  case WIFI_PROV_CRED_RECV: {
    auto *wifi_sta_cfg = static_cast<wifi_sta_config_t *>(event_data);
    ESP_LOGI(TAG, "Received credentials for SSID: %s",
             reinterpret_cast<const char *>(wifi_sta_cfg->ssid));

    publish_event(NetworkEvent::BleCredentialsReceived);

    // Store credentials using type-safe setters
    WifiCredentials creds{};
    creds.set_ssid(reinterpret_cast<const char *>(wifi_sta_cfg->ssid));
    creds.set_password(reinterpret_cast<const char *>(wifi_sta_cfg->password));

    (void)self->save_credentials(creds);
    self->pending_creds_ = creds;
    break;
  }

  case WIFI_PROV_CRED_FAIL: {
    auto *reason = static_cast<wifi_prov_sta_fail_reason_t *>(event_data);
    ESP_LOGE(TAG, "Provisioning failed: %s",
             (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Auth error"
                                                   : "AP not found");
    self->set_state(WifiState::Failed);
    publish_event(NetworkEvent::ProvisioningFailed);
    break;
  }

  case WIFI_PROV_CRED_SUCCESS:
    ESP_LOGI(TAG, "Provisioning successful");
    break;

  case WIFI_PROV_END:
    ESP_LOGI(TAG, "Provisioning ended");
    // Stop timeout timer - provisioning succeeded
    if (self->prov_timeout_timer_) {
      (void)self->prov_timeout_timer_->stop();
    }
    wifi_prov_mgr_deinit();
    self->prov_sub_.unsubscribe();
    publish_event(NetworkEvent::ProvisioningComplete);
    // State will change to Connected when IP is obtained
    break;

  default:
    break;
  }
}

} // namespace network
