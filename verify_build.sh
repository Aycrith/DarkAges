#!/bin/bash
# DarkAges MMO - Build Verification Script for Linux/macOS
# Run this script to verify the build works

set -e

echo "========================================"
echo "DarkAges MMO Build Verification"
echo "========================================"
echo

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found. Please install CMake 3.20 or later."
    echo "Ubuntu/Debian: sudo apt-get install cmake"
    echo "macOS: brew install cmake"
    exit 1
fi
echo "[OK] CMake found: $(cmake --version | head -n1)"

# Check for compiler
if command -v g++ &> /dev/null; then
    echo "[OK] GCC found: $(g++ --version | head -n1)"
    COMPILER="gcc"
elif command -v clang++ &> /dev/null; then
    echo "[OK] Clang found: $(clang++ --version | head -n1)"
    COMPILER="clang"
else
    echo "ERROR: No C++ compiler found."
    echo "Please install GCC or Clang"
    exit 1
fi

# Check C++20 support
echo "Checking C++20 support..."
cat > /tmp/cpp20_test.cpp << 'EOF'
#include <concepts>
int main() { return 0; }
EOF

if ! g++ -std=c++20 /tmp/cpp20_test.cpp -o /tmp/cpp20_test 2>/dev/null; then
    echo "WARNING: C++20 support may be incomplete"
fi

# Create build directory
mkdir -p build
cd build

# Configure
echo
echo "[1/4] Configuring CMake..."
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release

# Build
echo
echo "[2/4] Building..."
cmake --build . --parallel

# Run tests
echo
echo "[3/4] Running tests..."
ctest --output-on-failure

# Success
echo
echo "========================================"
echo "BUILD VERIFICATION SUCCESSFUL"
echo "========================================"
echo
echo "Binaries located in: build/"
echo "  - darkages_server"
echo "  - darkages_tests"
echo

cd ..
