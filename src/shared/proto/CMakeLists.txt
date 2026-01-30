# [NETWORK_AGENT] FlatBuffers Protocol CMake Configuration
# WP-6-4: FlatBuffers Protocol Implementation

cmake_minimum_required(VERSION 3.20)

# Protocol library - header-only since we use embedded flatbuffers
add_library(game_protocol INTERFACE)

target_include_directories(game_protocol INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../flatbuffers
)

# In a production setup, you would use flatc to generate code:
# find_program(FLATC_EXECUTABLE flatc)
# if(FLATC_EXECUTABLE)
#     add_custom_command(
#         OUTPUT game_protocol_generated.h
#         COMMAND ${FLATC_EXECUTABLE} --cpp -o ${CMAKE_CURRENT_SOURCE_DIR} 
#                 ${CMAKE_CURRENT_SOURCE_DIR}/game_protocol.fbs
#         DEPENDS game_protocol.fbs
#         COMMENT "Generating FlatBuffers code from game_protocol.fbs"
#     )
# endif()

# Export variables for parent CMakeLists.txt
set(FLATBUFFERS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../flatbuffers" CACHE PATH "FlatBuffers include directory")
set(GAME_PROTOCOL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Game protocol include directory")
