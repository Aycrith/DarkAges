# CMake Build Fixes Required

This document lists the known issues that need to be fixed to achieve a clean build.

## Critical Issues

### 1. Missing Dependency: GameNetworkingSockets

**Problem**: GNS is not included as a dependency

**Fix**: Add to CMakeLists.txt
```cmake
# Find or fetch GameNetworkingSockets
include(FetchContent)
FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG v1.4.1
)
FetchContent_MakeAvailable(GameNetworkingSockets)
```

### 2. Missing Dependency: FlatBuffers

**Problem**: FlatBuffers compiler not integrated

**Fix**: Add code generation step
```cmake
find_package(FlatBuffers REQUIRED)

# Generate protocol headers
set(PROTO_SCHEMA ${CMAKE_SOURCE_DIR}/src/shared/proto/game_protocol.fbs)
set(PROTO_OUTPUT ${CMAKE_BINARY_DIR}/generated/game_protocol_generated.h)

add_custom_command(
    OUTPUT ${PROTO_OUTPUT}
    COMMAND flatc --cpp -o ${CMAKE_BINARY_DIR}/generated ${PROTO_SCHEMA}
    DEPENDS ${PROTO_SCHEMA}
    COMMENT "Generating FlatBuffers protocol"
)

add_custom_target(protocol_generation DEPENDS ${PROTO_OUTPUT})
```

### 3. Missing Dependency: hiredis

**Problem**: Redis client library not linked

**Fix**:
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# Or use FetchContent
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(hiredis)
```

### 4. Missing Dependency: Catch2

**Problem**: Test framework not available

**Fix**:
```cmake
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)
```

### 5. EnTT Include Path

**Problem**: EnTT headers not found

**Fix**:
```cmake
# If using submodule
target_include_directories(darkages_server PRIVATE
    deps/entt/single_include
)
```

## Source Code Issues

### 6. Missing #include <span>

**Files affected**: Multiple files using `std::span`

**Fix**: Add to headers using span:
```cpp
#include <span>
```

### 7. Missing #include <chrono>

**Files affected**: Files using timing functions

**Fix**: Add:
```cpp
#include <chrono>
```

### 8. Missing #include <mutex>

**Files affected**: Thread-safe classes

**Fix**: Add:
```cpp
#include <mutex>
```

### 9. Forward Declaration Issues

**Problem**: Some headers use types without including their headers

**Fix**: Either add `#include` or use forward declarations consistently

## Platform-Specific Issues

### 10. Windows-Specific Code

**Problem**: Some networking code may be Windows-specific

**Fix**: Add platform abstractions:
```cpp
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
#endif
```

### 11. Compiler-Specific Features

**Problem**: Some C++20 features may not be available on all compilers

**Fix**: Check compiler versions in CMake:
```cmake
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
        message(FATAL_ERROR "GCC 11+ required for C++20")
    endif()
endif()
```

## Test Build Issues

### 12. Test Mocks

**Problem**: Some tests need mock implementations

**Fix**: Create mock classes:
```cpp
class MockNetworkManager : public NetworkManager {
public:
    MOCK_METHOD(bool, initialize, (uint16_t port), (override));
    // ...
};
```

## Suggested CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(DarkAges VERSION 0.1.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Dependencies
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# FetchContent for external deps
include(FetchContent)

# EnTT (submodule or FetchContent)
if(EXISTS ${CMAKE_SOURCE_DIR}/deps/entt)
    add_subdirectory(deps/entt)
else()
    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.12.2
    )
    FetchContent_MakeAvailable(EnTT)
endif()

# GameNetworkingSockets
FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG v1.4.1
)
FetchContent_MakeAvailable(GameNetworkingSockets)

# hiredis
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(hiredis)

# FlatBuffers
FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG v23.5.26
)
FetchContent_MakeAvailable(flatbuffers)

# Catch2 for testing
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

# Protocol generation
set(PROTO_SCHEMA ${CMAKE_SOURCE_DIR}/src/shared/proto/game_protocol.fbs)
set(PROTO_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

add_custom_command(
    OUTPUT ${PROTO_OUTPUT_DIR}/game_protocol_generated.h
    COMMAND flatbuffers::flatc --cpp -o ${PROTO_OUTPUT_DIR} ${PROTO_SCHEMA}
    DEPENDS ${PROTO_SCHEMA} flatbuffers::flatc
    COMMENT "Generating protocol code from ${PROTO_SCHEMA}"
)

add_custom_target(generate_protocol DEPENDS 
    ${PROTO_OUTPUT_DIR}/game_protocol_generated.h
)

# Server executable
file(GLOB_RECURSE SERVER_SOURCES src/server/src/*.cpp)
file(GLOB_RECURSE SERVER_HEADERS src/server/include/*.hpp)

add_executable(darkages_server ${SERVER_SOURCES} ${SERVER_HEADERS})
add_dependencies(darkages_server generate_protocol)

target_include_directories(darkages_server PRIVATE
    src/server/include
    ${PROTO_OUTPUT_DIR}
    deps/entt/single_include
)

target_link_libraries(darkages_server PRIVATE
    Threads::Threads
    EnTT::EnTT
    GameNetworkingSockets::GameNetworkingSockets
    hiredis
    flatbuffers::flatbuffers
)

# Tests
file(GLOB_RECURSE TEST_SOURCES src/server/tests/*.cpp)

add_executable(darkages_tests ${TEST_SOURCES})
add_dependencies(darkages_tests generate_protocol)

target_include_directories(darkages_tests PRIVATE
    src/server/include
    ${PROTO_OUTPUT_DIR}
    deps/entt/single_include
)

target_link_libraries(darkages_tests PRIVATE
    Catch2::Catch2WithMain
    EnTT::EnTT
    hiredis
    flatbuffers::flatbuffers
)

enable_testing()
add_test(NAME darkages_unit_tests COMMAND darkages_tests)
```

## Build Verification Checklist

- [ ] CMake configures without errors
- [ ] All dependencies are found or fetched
- [ ] FlatBuffers code generation works
- [ ] Server executable compiles
- [ ] No compiler warnings
- [ ] Tests compile
- [ ] Tests pass
- [ ] Docker image builds

## Next Steps

1. Run `./build_verify.sh` to see current status
2. Fix CMakeLists.txt with missing dependencies
3. Add missing `#include` directives to source files
4. Fix any platform-specific code
5. Re-run build verification
6. Commit working build configuration
