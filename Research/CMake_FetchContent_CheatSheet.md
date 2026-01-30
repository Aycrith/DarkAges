# CMake FetchContent Quick Reference Cheat Sheet

## Dependency Declaration Templates

### Basic Header-Only Library
```cmake
FetchContent_Declare(
    libname
    GIT_REPOSITORY https://github.com/user/repo.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libname)
target_link_libraries(your_target PRIVATE libname::libname)
```

### Library with Build Options
```cmake
# Set options BEFORE declare
set(LIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libname
    GIT_REPOSITORY https://github.com/user/repo.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libname)
```

### Try System Package First, Then Fetch
```cmake
FetchContent_Declare(
    libname
    GIT_REPOSITORY https://github.com/user/repo.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES LibName  # Try find_package first
)
FetchContent_MakeAvailable(libname)
```

### Override find_package Behavior
```cmake
FetchContent_Declare(
    libname
    GIT_REPOSITORY https://github.com/user/repo.git
    GIT_TAG v1.0.0
    OVERRIDE_FIND_PACKAGE  # All find_package(libname) will use this
)
FetchContent_MakeAvailable(libname)
```

---

## Windows-Specific Patterns

### Set MSVC Runtime (Do This First!)
```cmake
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
```

### Windows Socket Libraries
```cmake
if(WIN32)
    target_link_libraries(your_target PRIVATE
        ws2_32      # Winsock2
        winmm       # Windows multimedia
        crypt32     # Cryptographic functions
        iphlpapi    # IP Helper API
        bcrypt      # Windows crypto
    )
endif()
```

### OpenSSL Configuration
```cmake
if(WIN32)
    # Find OpenSSL
    find_package(OpenSSL QUIET)
    if(NOT OpenSSL_FOUND)
        message(WARNING "OpenSSL not found. Install with: winget install ShiningLight.OpenSSL")
    endif()
    
    # Match runtime library
    if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "Static")
        set(OPENSSL_MSVC_STATIC_RT ON CACHE BOOL "" FORCE)
    endif()
endif()
```

---

## Common Error Fixes

### LNK2038: Runtime Library Mismatch
```cmake
# Before any FetchContent_Declare
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
```

### Target Not Found After FetchContent
```cmake
# Create alias if missing
if(TARGET libname AND NOT TARGET libname::libname)
    add_library(libname::libname ALIAS libname)
endif()

# Or check and set manually
if(NOT TARGET libname::libname)
    message(FATAL_ERROR "libname::libname target not available")
endif()
```

### Include Directory Not Found
```cmake
# Use generator expression for flexibility
target_include_directories(your_target PRIVATE
    $<IF:$<TARGET_EXISTS:libname::libname>,
        ${libname_SOURCE_DIR}/include,
        ${LibName_INCLUDE_DIRS}
    >
)

# Or just use the source dir variable
target_include_directories(your_target PRIVATE
    ${libname_SOURCE_DIR}/include
)
```

### Undefined Reference / Link Error
```cmake
# Check link order - dependencies go after dependents
target_link_libraries(your_target PRIVATE
    # Your libraries
    my_library
    
    # External libraries (most dependent first)
    high_level_lib
    low_level_lib
    
    # System libraries last
    Threads::Threads
    OpenSSL::SSL
    ${CMAKE_DL_LIBS}
)
```

---

## Dependency-Specific Snippets

### GameNetworkingSockets
```cmake
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
set(USE_CRYPTO "OpenSSL" CACHE STRING "" FORCE)  # or "BCrypt" on Windows

FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG v1.4.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(GameNetworkingSockets)

target_link_libraries(your_target PRIVATE
    GameNetworkingSockets::GameNetworkingSockets
)
```

### FlatBuffers with Code Generation
```cmake
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG v24.3.25
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(flatbuffers)

# Code generation
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/schema_generated.h
    COMMAND flatbuffers::flatc --cpp -o ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/schema.fbs
    DEPENDS ${CMAKE_SOURCE_DIR}/schema.fbs flatbuffers::flatc
)
```

### hiredis
```cmake
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(hiredis)

# Create alias
target_link_libraries(your_target PRIVATE hiredis)
```

### nlohmann/json
```cmake
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

# Header-only, link or include
target_link_libraries(your_target PRIVATE nlohmann_json::nlohmann_json)
```

### Catch2
```cmake
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES Catch2
)
FetchContent_MakeAvailable(Catch2)

# Link with main
target_link_libraries(your_tests PRIVATE Catch2::Catch2WithMain)
```

---

## Build Commands

### Configure with Different Generators
```bash
# Visual Studio (Windows)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Ninja (cross-platform, faster)
cmake -B build -G Ninja

# Make (Linux/Mac)
cmake -B build -G "Unix Makefiles"
```

### Build Types
```bash
# Debug
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# RelWithDebInfo
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

### Verbose Output
```bash
cmake -B build --log-level=VERBOSE
cmake --build build --verbose
```

---

## Environment Variables and Cache Options

### Disable FetchContent Network Access (for offline/air-gapped)
```bash
# After first successful configure
cmake -B build -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

### Override Source Directory (for development)
```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_LIBNAME=/path/to/local/source
```

### Disable Updates (faster reconfigure)
```bash
cmake -B build -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
```

### Prefer System Packages
```bash
cmake -B build -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS
```

---

## CMake Presets Template

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "default",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_CXX_STANDARD": "20",
        "FETCH_DEPENDENCIES": "ON"
      }
    },
    {
      "name": "windows-msvc",
      "inherits": "default",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      }
    },
    {
      "name": "windows-ninja",
      "inherits": "default",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      }
    },
    {
      "name": "linux",
      "inherits": "default",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Linux"
      }
    },
    {
      "name": "vcpkg",
      "inherits": "default",
      "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
      "cacheVariables": {
        "VCPKG_TARGET_TRIPLET": "x64-windows-static"
      }
    }
  ]
}
```

Usage:
```bash
cmake --preset windows-msvc
cmake --build build/windows-msvc
```

---

## Quick Debugging

### List All Targets
```bash
cmake --build build --target help
```

### Print Variable Values
```cmake
message(STATUS "VAR_NAME = ${VAR_NAME}")
message(STATUS "Target exists: $<TARGET_EXISTS:target_name>")
```

### Check Dependency Location
```bash
ls build/_deps/
cat build/CMakeCache.txt | grep -i "source_dir"
```

### Verify Compiler Flags
```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# Check compile_commands.json
```

---

## Best Practices Checklist

- [ ] Set MSVC runtime before any FetchContent calls
- [ ] Use `GIT_SHALLOW TRUE` for faster clones
- [ ] Pin to specific versions with `GIT_TAG`
- [ ] Disable unnecessary targets (tests, examples)
- [ ] Use `FIND_PACKAGE_ARGS` to prefer system packages
- [ ] Create consistent `::` aliases for all dependencies
- [ ] Set `FETCHCONTENT_QUIET ON` for cleaner logs
- [ ] Test both FetchContent and system package paths
- [ ] Document version requirements
- [ ] Use `OVERRIDE_FIND_PACKAGE` when integrating with find_package-using code

---

*Quick Reference for DarkAges MMO Project*
