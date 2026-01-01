# I2C master driver

idf_component_register(
    SRCS
        "src/i2c.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_driver_i2c
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
