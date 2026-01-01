# Bosch BSEC2 Library
# Source: https://github.com/boschsensortec/Bosch-BSEC2-Library
#
# This component uses files copied from the Bosch-BSEC2-Library submodule.
# Run 'go run tools/setup/main.go' to configure BSEC for your target.

idf_component_register(
    INCLUDE_DIRS "include"
)

# Link the pre-compiled BSEC library (copied by setup tool)
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libalgobsec.a"
)
