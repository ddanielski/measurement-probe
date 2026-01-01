# Library Layer - Network Component
# ==================================
# WiFi management, transport abstractions (HTTP/MQTT)
# Reusable across any ESP-IDF IoT project

idf_component_register(
    SRCS
        "src/placeholder.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        core  # Our core wrappers
        esp_wifi
        esp_event
        esp_netif
        esp-tls
        esp_http_client
        # mqtt  # Uncomment when MQTT transport is implemented
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)

