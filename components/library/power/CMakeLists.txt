# Power component - sleep, power management

idf_component_register(
    SRCS
        "src/power.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_hw_support
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
