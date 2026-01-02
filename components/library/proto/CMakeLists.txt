# Proto component - protobuf messages and adapters
#
# Contains:
#   - nanopb generated code (measurement.pb.h/c)
#   - C++ adapters for domain model conversion

idf_component_register(
    SRCS
        "src/measurement.pb.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nikas-belogolov__nanopb
        sensor_base
)
