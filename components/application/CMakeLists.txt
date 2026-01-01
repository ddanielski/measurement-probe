# Application-specific logic component
# Not intended for reuse in other projects

idf_component_register(
    SRCS
        "src/board.cpp"
        "src/app.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/main"  # For app_config.hpp
    REQUIRES
        core
        i2c
        power
        sensor_base
        bme680_sensor
)
