#!/bin/bash
# ============================================================================
# DarkAges MMO Build Script (Unix/Linux/macOS)
# ============================================================================
# Usage:
#   ./build.sh [options]
#
# Options:
#   -t, --target <Target>        Build target: Server, Client, All (default: All)
#   -c, --config <Config>        Build configuration: Debug, Release (default: Release)
#   --clean                      Clean before building
#   --test                       Run tests after build
#   --package                    Create release package
#   -v, --verbose                Verbose output
#   -V, --version <Version>      Override version (defaults to VERSION file)
#   -h, --help                   Show this help
#
# Examples:
#   ./build.sh -t Server -c Debug --test
#   ./build.sh --clean --package
# ============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
TARGET="All"
CONFIGURATION="Release"
CLEAN=false
TEST=false
PACKAGE=false
VERBOSE=false
VERSION=""

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"
ARTIFACTS_DIR="$PROJECT_ROOT/artifacts"
VERSION_FILE="$PROJECT_ROOT/VERSION"

# Functions
print_status() {
    echo -e "${GREEN}[+]${NC} $1"
}

print_error() {
    echo -e "${RED}[!]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[*]${NC} $1"
}

print_info() {
    echo -e "${BLUE}[>]${NC} $1"
}

print_section() {
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}$1${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
}

show_help() {
    head -n 30 "$0" | tail -n 28
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -c|--config)
            CONFIGURATION="$2"
            shift 2
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --test)
            TEST=true
            shift
            ;;
        --package)
            PACKAGE=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -V|--version)
            VERSION="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Validate target
if [[ "$TARGET" != "Server" && "$TARGET" != "Client" && "$TARGET" != "All" ]]; then
    print_error "Invalid target: $TARGET. Must be Server, Client, or All."
    exit 1
fi

# Validate configuration
if [[ "$CONFIGURATION" != "Debug" && "$CONFIGURATION" != "Release" ]]; then
    print_error "Invalid configuration: $CONFIGURATION. Must be Debug or Release."
    exit 1
fi

# Read version
if [[ -z "$VERSION" ]]; then
    if [[ -f "$VERSION_FILE" ]]; then
        VERSION=$(cat "$VERSION_FILE" | tr -d '[:space:]')
    else
        VERSION="0.1.0"
    fi
fi

# Print banner
print_section "DarkAges MMO Build System v$VERSION"
print_info "Target: $TARGET"
print_info "Configuration: $CONFIGURATION"
print_info "Project Root: $PROJECT_ROOT"
echo ""

# Detect OS
OS="unknown"
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    OS="windows"
fi

print_info "Detected OS: $OS"

# Clean
if [[ "$CLEAN" == true ]]; then
    print_status "Cleaning build directories..."
    rm -rf "$BUILD_DIR" "$ARTIFACTS_DIR"
    print_info "Cleaned build directories"
    echo ""
fi

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$ARTIFACTS_DIR"

# Detect compiler
COMPILER="unknown"
if command -v g++ &> /dev/null; then
    COMPILER="GCC"
elif command -v clang++ &> /dev/null; then
    COMPILER="Clang"
fi

print_info "Detected compiler: $COMPILER"

# ============================================================================
# Build Server
# ============================================================================
if [[ "$TARGET" == "Server" || "$TARGET" == "All" ]]; then
    print_section "Building Server ($CONFIGURATION)"
    
    # Check prerequisites
    print_status "Checking prerequisites..."
    
    if ! command -v cmake &> /dev/null; then
        print_error "CMake not found. Please install CMake 3.20 or later."
        exit 1
    fi
    CMAKE_VERSION=$(cmake --version | head -n1)
    print_info "CMake: $CMAKE_VERSION"
    
    if [[ "$COMPILER" == "unknown" ]]; then
        print_error "No C++ compiler found. Please install GCC or Clang."
        exit 1
    fi
    print_info "Compiler: $COMPILER"
    
    # Check dependencies
    print_status "Checking dependencies..."
    DEPS_OK=true
    
    if [[ ! -f "$PROJECT_ROOT/deps/entt/single_include/entt/entt.hpp" ]]; then
        print_warning "EnTT not found. Fetching..."
        git clone --depth 1 --branch v3.13.0 https://github.com/skypjack/entt.git "$PROJECT_ROOT/deps/entt" 2>&1 | tail -5
        if [[ $? -eq 0 ]]; then
            print_info "EnTT: Fetched"
        else
            DEPS_OK=false
        fi
    else
        print_info "EnTT: OK"
    fi
    
    if [[ ! -f "$PROJECT_ROOT/deps/glm/glm/glm.hpp" ]]; then
        print_warning "GLM not found. Fetching..."
        git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git "$PROJECT_ROOT/deps/glm" 2>&1 | tail -5
        if [[ $? -eq 0 ]]; then
            print_info "GLM: Fetched"
        else
            DEPS_OK=false
        fi
    else
        print_info "GLM: OK"
    fi
    
    if [[ "$DEPS_OK" == false ]]; then
        print_error "Failed to fetch dependencies"
        exit 1
    fi
    
    # Configure CMake
    print_status "Configuring CMake..."
    cd "$BUILD_DIR"
    
    CMAKE_ARGS=(
        ".."
        "-DCMAKE_BUILD_TYPE=$CONFIGURATION"
        "-DBUILD_TESTS=ON"
        "-DBUILD_SHARED_LIBS=OFF"
        "-G" "Ninja"
    )
    
    # Fall back to Make if Ninja not available
    if ! command -v ninja &> /dev/null; then
        CMAKE_ARGS=("${CMAKE_ARGS[@]//Ninja/Unix Makefiles}")
        print_info "Ninja not found, using Make"
    fi
    
    if [[ "$VERBOSE" == true ]]; then
        CMAKE_ARGS+=("-DCMAKE_VERBOSE_MAKEFILE=ON")
    fi
    
    cmake "${CMAKE_ARGS[@]}" 2>&1
    if [[ $? -ne 0 ]]; then
        print_error "CMake configuration failed"
        cd "$PROJECT_ROOT"
        exit 1
    fi
    print_info "CMake configuration successful"
    
    # Build
    print_status "Building server..."
    
    if command -v ninja &> /dev/null; then
        BUILD_CMD="ninja -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    else
        BUILD_CMD="make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    fi
    
    eval "$BUILD_CMD" 2>&1
    if [[ $? -ne 0 ]]; then
        print_error "Build failed"
        cd "$PROJECT_ROOT"
        exit 1
    fi
    
    print_status "Server build complete"
    cd "$PROJECT_ROOT"
fi

# ============================================================================
# Run Tests
# ============================================================================
if [[ "$TEST" == true ]] && [[ "$TARGET" == "Server" || "$TARGET" == "All" ]]; then
    print_section "Running Tests"
    
    cd "$BUILD_DIR"
    ctest --output-on-failure 2>&1
    
    if [[ $? -ne 0 ]]; then
        print_error "Tests failed"
        cd "$PROJECT_ROOT"
        exit 1
    fi
    
    print_status "All tests passed"
    cd "$PROJECT_ROOT"
fi

# ============================================================================
# Build Client
# ============================================================================
if [[ "$TARGET" == "Client" || "$TARGET" == "All" ]]; then
    print_section "Building Client"
    
    CLIENT_DIR="$PROJECT_ROOT/src/client"
    
    if [[ ! -d "$CLIENT_DIR" ]]; then
        print_warning "Client directory not found at $CLIENT_DIR - skipping client build"
    else
        # Check for Godot
        GODOT_EXE=""
        for variant in godot godot4 Godot_v4 godot.exe Godot.exe; do
            if command -v "$variant" &> /dev/null; then
                GODOT_EXE="$variant"
                break
            fi
        done
        
        if [[ -z "$GODOT_EXE" ]]; then
            print_warning "Godot not found in PATH. Skipping client build."
            print_info "Please install Godot 4.2+ and add it to PATH"
        else
            print_info "Using Godot: $GODOT_EXE"
            
            cd "$CLIENT_DIR"
            
            # Build C# solutions
            print_status "Building C# solutions..."
            if command -v dotnet &> /dev/null; then
                dotnet restore 2>&1 | tail -10
            fi
            
            "$GODOT_EXE" --build-solutions --headless 2>&1
            if [[ $? -ne 0 ]]; then
                print_error "Client C# build failed"
                cd "$PROJECT_ROOT"
                exit 1
            fi
            
            # Export if packaging
            if [[ "$PACKAGE" == true ]]; then
                print_status "Exporting client..."
                EXPORT_DIR="$ARTIFACTS_DIR/client"
                mkdir -p "$EXPORT_DIR"
                
                # Determine platform
                PLATFORM="Linux/X11"
                EXT=".x86_64"
                if [[ "$OS" == "macos" ]]; then
                    PLATFORM="macOS"
                    EXT=".app"
                elif [[ "$OS" == "windows" ]]; then
                    PLATFORM="Windows Desktop"
                    EXT=".exe"
                fi
                
                EXPORT_PATH="$EXPORT_DIR/DarkAgesClient$EXT"
                "$GODOT_EXE" --export-release "$PLATFORM" "$EXPORT_PATH" --headless 2>&1 || \
                    print_warning "Client export may have issues, but build succeeded"
            fi
            
            print_status "Client build complete"
            cd "$PROJECT_ROOT"
        fi
    fi
fi

# ============================================================================
# Package Release
# ============================================================================
if [[ "$PACKAGE" == true ]]; then
    print_section "Creating Release Package"
    
    PACKAGE_DIR="$ARTIFACTS_DIR/DarkAges-v$VERSION"
    mkdir -p "$PACKAGE_DIR"
    
    # Server binaries
    SERVER_OUT="$PACKAGE_DIR/server"
    mkdir -p "$SERVER_OUT"
    
    # Find and copy server executable
    SERVER_FOUND=false
    for exe_path in \
        "$BUILD_DIR/$CONFIGURATION/darkages_server" \
        "$BUILD_DIR/darkages_server" \
        "$BUILD_DIR/Release/darkages_server"; do
        if [[ -f "$exe_path" ]]; then
            cp "$exe_path" "$SERVER_OUT/"
            print_info "Copied server binary: $(basename "$exe_path")"
            SERVER_FOUND=true
            break
        fi
    done
    
    if [[ "$SERVER_FOUND" == false ]]; then
        # Try Windows exe
        for exe_path in \
            "$BUILD_DIR/$CONFIGURATION/darkages_server.exe" \
            "$BUILD_DIR/darkages_server.exe" \
            "$BUILD_DIR/Release/darkages_server.exe"; do
            if [[ -f "$exe_path" ]]; then
                cp "$exe_path" "$SERVER_OUT/"
                print_info "Copied server binary: $(basename "$exe_path")"
                SERVER_FOUND=true
                break
            fi
        done
    fi
    
    if [[ "$SERVER_FOUND" == false ]]; then
        print_warning "Server binary not found in build directory"
    fi
    
    # Copy config files
    if [[ -d "$PROJECT_ROOT/infra" ]]; then
        for file in "$PROJECT_ROOT"/infra/docker-compose*.yml; do
            if [[ -f "$file" ]]; then
                cp "$file" "$SERVER_OUT/"
                print_info "Copied: $(basename "$file")"
            fi
        done
    fi
    
    # Copy scripts
    for script in quickstart.ps1 verify_build.sh verify_build.bat; do
        if [[ -f "$PROJECT_ROOT/$script" ]]; then
            cp "$PROJECT_ROOT/$script" "$SERVER_OUT/"
            print_info "Copied: $script"
        fi
    done
    
    # Copy client
    if [[ -d "$ARTIFACTS_DIR/client" ]]; then
        cp -r "$ARTIFACTS_DIR/client" "$PACKAGE_DIR/"
        print_info "Copied client artifacts"
    fi
    
    # Copy documentation
    for doc in README.md LICENSE CHANGELOG.md CONTRIBUTING.md; do
        if [[ -f "$PROJECT_ROOT/$doc" ]]; then
            cp "$PROJECT_ROOT/$doc" "$PACKAGE_DIR/"
            print_info "Copied: $doc"
        fi
    done
    
    # Create version info
    cat > "$PACKAGE_DIR/VERSION.txt" << EOF
DarkAges MMO v$VERSION
Build Date: $(date '+%Y-%m-%d %H:%M:%S')
Configuration: $CONFIGURATION
Compiler: $COMPILER
EOF
    
    # Create archive
    ARCHIVE_NAME="DarkAges-v$VERSION-$OS.tar.gz"
    ARCHIVE_PATH="$ARTIFACTS_DIR/$ARCHIVE_NAME"
    
    print_status "Creating archive: $ARCHIVE_NAME"
    cd "$ARTIFACTS_DIR"
    tar -czf "$ARCHIVE_NAME" "DarkAges-v$VERSION"
    cd "$PROJECT_ROOT"
    
    print_status "Package created: $ARCHIVE_PATH"
    
    # Calculate checksum
    if command -v sha256sum &> /dev/null; then
        sha256sum "$ARCHIVE_PATH" > "$ARCHIVE_PATH.sha256"
        CHECKSUM=$(sha256sum "$ARCHIVE_PATH" | awk '{print $1}')
        print_info "SHA256: $CHECKSUM"
    elif command -v shasum &> /dev/null; then
        shasum -a 256 "$ARCHIVE_PATH" > "$ARCHIVE_PATH.sha256"
        CHECKSUM=$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')
        print_info "SHA256: $CHECKSUM"
    fi
fi

# ============================================================================
# Summary
# ============================================================================
print_section "Build Summary"

echo "Version: $VERSION"
echo "Target: $TARGET"
echo "Configuration: $CONFIGURATION"
echo "Compiler: $COMPILER"
echo ""

if [[ "$TARGET" == "Server" || "$TARGET" == "All" ]]; then
    echo -e "${CYAN}Server:${NC}"
    SERVER_EXE_FOUND=false
    for path in \
        "$BUILD_DIR/$CONFIGURATION/darkages_server" \
        "$BUILD_DIR/darkages_server" \
        "$BUILD_DIR/darkages_server.exe"; do
        if [[ -f "$path" ]]; then
            echo -e "  ${GREEN}✓${NC} $path"
            SERVER_EXE_FOUND=true
            break
        fi
    done
    if [[ "$SERVER_EXE_FOUND" == false ]]; then
        echo -e "  ${RED}✗${NC} Not found"
    fi
fi

if [[ "$TEST" == true ]]; then
    echo ""
    echo -e "Tests: ${GREEN}✓ PASSED${NC}"
fi

if [[ "$PACKAGE" == true ]]; then
    echo ""
    echo -e "${CYAN}Artifacts:${NC}"
    if [[ -d "$ARTIFACTS_DIR" ]]; then
        find "$ARTIFACTS_DIR" -type f -exec ls -lh {} \; | awk '{print "  -", $9, "(" $5 ")"}'
    fi
fi

echo ""
print_status "Build completed successfully!"
echo ""
