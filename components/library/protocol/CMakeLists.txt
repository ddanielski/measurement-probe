# Library Layer - Protocol Component
# ====================================
# Protobuf encoding/decoding, message handling
# Reusable for any Protobuf-based IoT project

idf_component_register(
    SRCS
        "src/placeholder.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        core
        # nanopb  # Add when nanopb component is integrated
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)

