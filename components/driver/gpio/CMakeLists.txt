# Driver Layer - GPIO Component
# ==============================
# Type-safe GPIO abstraction
# Low-level driver for pin configuration and control

idf_component_register(
    SRCS
        "src/placeholder.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver  # ESP-IDF GPIO driver
        esp_common
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)

