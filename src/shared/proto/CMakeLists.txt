# Protobuf Generation for DarkAges Protocol
# [NETWORK_AGENT] Generates C++ and C# bindings from .proto files

cmake_minimum_required(VERSION 3.20)

# Find Protobuf package
find_package(Protobuf QUIET)

if(NOT Protobuf_FOUND)
    message(WARNING "Protobuf not found - protocol generation disabled")
    return()
endif()

# Protobuf files
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/DarkAgesProtocol.proto
)

# Output directories
set(PROTO_CPP_OUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/cpp)
set(PROTO_CS_OUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/cs)

file(MAKE_DIRECTORY ${PROTO_CPP_OUT})
file(MAKE_DIRECTORY ${PROTO_CS_OUT})

# Generate C++
add_custom_command(
    OUTPUT ${PROTO_CPP_OUT}/DarkAgesProtocol.pb.cc ${PROTO_CPP_OUT}/DarkAgesProtocol.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --cpp_out=${PROTO_CPP_OUT}
         --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating C++ protocol bindings"
)

# Generate C#
add_custom_command(
    OUTPUT ${PROTO_CS_OUT}/DarkAgesProtocol.cs
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --csharp_out=${PROTO_CS_OUT}
         --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating C# protocol bindings"
)

# Create library
add_library(darkages_protocol STATIC
    ${PROTO_CPP_OUT}/DarkAgesProtocol.pb.cc
    ${PROTO_CPP_OUT}/DarkAgesProtocol.pb.h
)

target_include_directories(darkages_protocol PUBLIC
    ${PROTO_CPP_OUT}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(darkages_protocol PUBLIC
    ${Protobuf_LIBRARIES}
)

# Disable warnings for generated code
if(MSVC)
    target_compile_options(darkages_protocol PRIVATE /W0)
else()
    target_compile_options(darkages_protocol PRIVATE -w)
endif()

message(STATUS "Protocol: Protobuf generation configured")
