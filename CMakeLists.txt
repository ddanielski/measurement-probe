# Measurement Probe - ESP32-C3 Cloud Gateway

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# EXTRA_COMPONENT_DIRS: directories that contain component subdirectories
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components/library"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/driver"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/external"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/sensor"
)

# Include ESP-IDF CMake
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(measurement_probe)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Post-build: Copy compile_commands.json to project root for clangd
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Copying compile_commands.json to project root"
)

