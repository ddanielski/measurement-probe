# Core library - RAII wrappers and utilities

idf_component_register(
    SRCS "src/core.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        esp_timer
        esp_event
        nvs_flash
        esp_driver_gpio
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
