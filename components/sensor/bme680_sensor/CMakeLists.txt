# BME680 sensor implementation with BSEC

idf_component_register(
    SRCS
        "src/sensor.cpp"
        "src/bsec_wrapper.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bme680
        bsec2
        sensor_base
        core
        log
        esp_timer
)
