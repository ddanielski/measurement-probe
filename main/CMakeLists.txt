idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        application
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
