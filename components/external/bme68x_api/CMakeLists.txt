# Bosch BME68x Sensor API
# Source: https://github.com/boschsensortec/BME68x_SensorAPI
# License: BSD-3-Clause
#
# This component references the BME68x_SensorAPI git submodule.
# Run 'go run tools/setup/main.go' to initialize submodules.

set(SUBMODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../BME68x_SensorAPI")

idf_component_register(
    SRCS "${SUBMODULE_PATH}/bme68x.c"
    INCLUDE_DIRS "${SUBMODULE_PATH}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
