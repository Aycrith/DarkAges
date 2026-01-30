#!/bin/bash
# DarkAges MMO - Build Script for Linux/macOS
# Usage: ./build.sh [Debug|Release] [options]

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
GRAY='\033[0;90m'
NC='\033[0m' # No Color

# Defaults
BUILD_TYPE="Release"
TESTS=OFF
SANITIZERS=OFF
CLEAN=0
SERVER=ON
CLIENT=OFF
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Help function
show_help() {
    cat << EOF
DarkAges MMO Build Script

Usage: ./build.sh [BuildType] [options]

BuildType:
    Debug    - Build with debug symbols and no optimization
    Release  - Build with optimizations (default)

Options:
    --tests       - Build unit tests
    --sanitizers  - Enable AddressSanitizer (Debug only)
    --clean       - Clean build directory first
    --client      - Build client only (requires Godot)
    --jobs N      - Number of parallel build jobs (default: $JOBS)
    --help        - Show this help message

Examples:
    ./build.sh Debug --tests --sanitizers
    ./build.sh Release --tests --clean
    ./build.sh --clean
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        Debug|Release)
            BUILD_TYPE="$1"
            shift
            ;;
        --tests)
            TESTS=ON
            shift
            ;;
        --sanitizers)
            SANITIZERS=ON
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --client)
            CLIENT=ON
            shift
            ;;
        --server)
            SERVER=ON
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

# Determine project root
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

echo -e "${CYAN}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║         DarkAges MMO - Build System                      ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${YELLOW}Build Type: $BUILD_TYPE${NC}"
echo -e "${GRAY}Project Root: $PROJECT_ROOT${NC}"
echo -e "${GRAY}Build Directory: $BUILD_DIR${NC}"
echo -e "${GRAY}Parallel Jobs: $JOBS${NC}"
echo ""

# Clean if requested
if [ $CLEAN -eq 1 ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# CMake options
CMAKE_OPTIONS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DBUILD_SERVER="$SERVER"
    -DBUILD_TESTS="$TESTS"
    -DBUILD_CLIENT="$CLIENT"
    -DENABLE_SANITIZERS="$SANITIZERS"
)

# Configure
echo -e "${GREEN}Configuring with CMake...${NC}"
echo -e "${GRAY}Options: ${CMAKE_OPTIONS[*]}${NC}"
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" "${CMAKE_OPTIONS[@]}"

# Build
echo ""
echo -e "${GREEN}Building project...${NC}"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
echo -e "${GREEN}══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}                 Build Complete!                          ${NC}"
echo -e "${GREEN}══════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${YELLOW}Executables:${NC}"

# Find built executables
if [ -d "$BUILD_DIR/bin" ]; then
    find "$BUILD_DIR/bin" -type f -executable -print | while read -r exe; do
        echo -e "${GRAY}  - $exe${NC}"
    done
fi

# Run tests if requested
if [ "$TESTS" == "ON" ]; then
    echo ""
    echo -e "${GREEN}Running tests...${NC}"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

echo ""
echo -e "${GREEN}Done!${NC}"
