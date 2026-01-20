# Cloud library - Backend communication and device authentication

idf_component_register(
    SRCS
        "src/device_auth.cpp"
        "src/cloud_client.cpp"
        "src/command_handler.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        core
        transport
        proto
        sensor_base
        freertos
        nvs_flash
        esp_http_client
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
