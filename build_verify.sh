#!/bin/bash
# DarkAges MMO - Build Verification Script
# Run this to check the current build status

set -e

echo "=========================================="
echo "DarkAges Build Verification"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

FAILED=0

# Check dependencies
echo "[1/5] Checking dependencies..."

if command -v cmake &> /dev/null; then
    echo -e "${GREEN}✓${NC} CMake found: $(cmake --version | head -n1)"
else
    echo -e "${RED}✗${NC} CMake not found. Install CMake 3.20+"
    FAILED=1
fi

if command -v g++ &> /dev/null; then
    echo -e "${GREEN}✓${NC} g++ found: $(g++ --version | head -n1)"
else
    echo -e "${RED}✗${NC} g++ not found. Install GCC 11+"
    FAILED=1
fi

if [ -d "deps/entt" ]; then
    echo -e "${GREEN}✓${NC} EnTT submodule found"
else
    echo -e "${YELLOW}!${NC} EnTT submodule missing. Run: git submodule update --init"
fi

echo ""
echo "[2/5] Checking source files..."

SERVER_SOURCES=$(find src/server -name "*.cpp" | wc -l)
SERVER_HEADERS=$(find src/server -name "*.hpp" | wc -l)
echo "  Server sources: $SERVER_SOURCES .cpp files"
echo "  Server headers: $SERVER_HEADERS .hpp files"

# Check for common issues
echo ""
echo "[3/5] Checking for common issues..."

# Check for pragma once in headers
HEADERS_WITHOUT_PRAGMA=$(grep -L "#pragma once" src/server/include/*.hpp src/server/include/*/*.hpp 2>/dev/null | wc -l)
if [ "$HEADERS_WITHOUT_PRAGMA" -gt 0 ]; then
    echo -e "${YELLOW}!${NC} $HEADERS_WITHOUT_PRAGMA headers missing #pragma once"
fi

# Check for include guards (alternative to pragma once)
HEADERS_WITH_GUARDS=$(grep -l "#ifndef" src/server/include/*.hpp src/server/include/*/*.hpp 2>/dev/null | wc -l)
if [ "$HEADERS_WITH_GUARDS" -gt 0 ]; then
    echo -e "${GREEN}✓${NC} $HEADERS_WITH_GUARDS headers have include guards"
fi

echo ""
echo "[4/5] Attempting CMake configuration..."

mkdir -p build
cd build

if cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | tee cmake.log; then
    echo -e "${GREEN}✓${NC} CMake configuration succeeded"
    
    echo ""
    echo "[5/5] Attempting build..."
    
    if make -j$(nproc) 2>&1 | tee build.log; then
        echo -e "${GREEN}✓${NC} Build succeeded!"
        echo ""
        echo "=========================================="
        echo "Build Verification: PASSED"
        echo "=========================================="
    else
        echo -e "${RED}✗${NC} Build failed!"
        echo ""
        echo "=========================================="
        echo "Build Verification: FAILED"
        echo "=========================================="
        echo ""
        echo "Build errors:"
        grep -E "(error:|Error:)" build.log | head -20
        FAILED=1
    fi
else
    echo -e "${RED}✗${NC} CMake configuration failed!"
    echo ""
    echo "CMake errors:"
    grep -i "error" cmake.log | head -20
    FAILED=1
fi

cd ..

echo ""
echo "=========================================="
echo "Summary"
echo "=========================================="

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All checks passed!${NC}"
    exit 0
else
    echo -e "${RED}Some checks failed.${NC}"
    echo ""
    echo "Next steps:"
    echo "1. Review errors in build/build.log"
    echo "2. Fix CMakeLists.txt dependencies"
    echo "3. Add missing #include directives"
    echo "4. Run ./build_verify.sh again"
    exit 1
fi
