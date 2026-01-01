# BME680 driver wrapping Bosch API

idf_component_register(
    SRCS "src/driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bme68x_api
        i2c
        driver_base
        freertos
        log
)
