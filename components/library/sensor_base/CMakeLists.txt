# Sensor interface and manager

idf_component_register(
    SRCS
        "src/sensor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core network
)
