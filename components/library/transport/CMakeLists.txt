# Transport library - Cloud communication abstraction

idf_component_register(
    SRCS "src/placeholder.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        core
        freertos
        esp_http_client
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
