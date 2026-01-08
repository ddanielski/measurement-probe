# Library Layer - Network Component
# ==================================
# WiFi management, transport abstractions (HTTP/MQTT)
# Reusable across any ESP-IDF IoT project

idf_component_register(
    SRCS
        "src/wifi_manager.cpp"
        "src/sntp.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        core
        esp_wifi
        esp_event
        esp_netif
        esp-tls
        esp_http_client
        wifi_provisioning
        bt
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
