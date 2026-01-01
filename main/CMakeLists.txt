idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        # Library layer
        core
        power
        sensor_base

        # Driver layer
        i2c
        bme680

        # Sensor layer
        bme680_sensor

        # ESP-IDF
        log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
    -Wno-missing-field-initializers
)
