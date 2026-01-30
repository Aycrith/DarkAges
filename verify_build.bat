@echo off
REM DarkAges MMO - Build Verification Script for Windows
REM Run this script to verify the build works

echo ========================================
echo DarkAges MMO Build Verification
echo ========================================
echo.

REM Check for CMake
cmake --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found. Please install CMake 3.20 or later.
    echo Download from: https://cmake.org/download/
    exit /b 1
)
echo [OK] CMake found

REM Check for compiler
where cl >nul 2>&1
if not errorlevel 1 (
    echo [OK] MSVC compiler found
    set COMPILER=msvc
    goto compiler_found
)

where g++ >nul 2>&1
if not errorlevel 1 (
    echo [OK] GCC compiler found
    set COMPILER=gcc
    goto compiler_found
)

where clang++ >nul 2>&1
if not errorlevel 1 (
    echo [OK] Clang compiler found
    set COMPILER=clang
    goto compiler_found
)

echo ERROR: No C++ compiler found.
echo Please install one of:
echo   - Visual Studio 2022 with C++ workload
echo   - MinGW-w64 GCC
echo   - LLVM/Clang
exit /b 1

:compiler_found

REM Create build directory
if not exist build mkdir build
cd build

REM Configure
echo.
echo [1/4] Configuring CMake...
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo ERROR: CMake configuration failed
    exit /b 1
)

REM Build
echo.
echo [2/4] Building...
cmake --build . --parallel
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

REM Run tests
echo.
echo [3/4] Running tests...
ctest --output-on-failure
if errorlevel 1 (
    echo ERROR: Tests failed
    exit /b 1
)

REM Success
echo.
echo ========================================
echo BUILD VERIFICATION SUCCESSFUL
echo ========================================
echo.
echo Binaries located in: build\Release\
echo   - darkages_server.exe
echo   - darkages_tests.exe
echo.

cd ..
exit /b 0
