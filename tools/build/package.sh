#!/bin/bash
# ============================================================================
# DarkAges MMO - Local Packaging Script
# ============================================================================
# Creates a release package from existing build artifacts
#
# Usage:
#   ./tools/build/package.sh [version]
#
# Examples:
#   ./tools/build/package.sh          # Use VERSION file
#   ./tools/build/package.sh 0.8.0    # Override version
# ============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

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

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
ARTIFACTS_DIR="$PROJECT_ROOT/artifacts"
VERSION_FILE="$PROJECT_ROOT/VERSION"

# Get version
VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
    if [[ -f "$VERSION_FILE" ]]; then
        VERSION=$(cat "$VERSION_FILE" | tr -d '[:space:]')
    else
        VERSION="0.1.0"
    fi
fi

print_section "DarkAges MMO Packaging Script"
print_info "Version: $VERSION"
print_info "Project Root: $PROJECT_ROOT"

# Check for build artifacts
if [[ ! -d "$BUILD_DIR" ]]; then
    print_error "Build directory not found: $BUILD_DIR"
    print_info "Run ./build.sh first"
    exit 1
fi

# Detect OS
OS="linux"
if [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    OS="win64"
fi

# Create directories
mkdir -p "$ARTIFACTS_DIR"

PACKAGE_DIR="$ARTIFACTS_DIR/DarkAges-v$VERSION"
if [[ -d "$PACKAGE_DIR" ]]; then
    print_warning "Removing existing package directory"
    rm -rf "$PACKAGE_DIR"
fi

mkdir -p "$PACKAGE_DIR"

# ============================================================================
# Package Server
# ============================================================================
print_section "Packaging Server"

SERVER_OUT="$PACKAGE_DIR/server"
mkdir -p "$SERVER_OUT"

# Find server executable
SERVER_FOUND=false
for exe_name in darkages_server darkages_server.exe; do
    for config in Release Debug ""; do
        if [[ -n "$config" ]]; then
            path="$BUILD_DIR/$config/$exe_name"
        else
            path="$BUILD_DIR/$exe_name"
        fi
        
        if [[ -f "$path" ]]; then
            cp "$path" "$SERVER_OUT/"
            print_info "Copied: $(basename "$path")"
            SERVER_FOUND=true
            break 2
        fi
    done
done

if [[ "$SERVER_FOUND" == false ]]; then
    print_error "Server executable not found in $BUILD_DIR"
    exit 1
fi

# Copy DLLs/so files
for ext in dll so dylib; do
    for file in "$BUILD_DIR"/*.$ext; do
        if [[ -f "$file" ]]; then
            cp "$file" "$SERVER_OUT/"
            print_info "Copied: $(basename "$file")"
        fi
    done
done

# Copy configuration files
if [[ -d "$PROJECT_ROOT/infra" ]]; then
    for file in "$PROJECT_ROOT"/infra/docker-compose*.yml; do
        if [[ -f "$file" ]]; then
            cp "$file" "$SERVER_OUT/"
            print_info "Copied: $(basename "$file")"
        fi
    done
fi

# Copy scripts
SCRIPTS=("quickstart.ps1" "verify_build.sh" "verify_build.bat")
for script in "${SCRIPTS[@]}"; do
    if [[ -f "$PROJECT_ROOT/$script" ]]; then
        cp "$PROJECT_ROOT/$script" "$SERVER_OUT/"
        print_info "Copied: $script"
    fi
done

# ============================================================================
# Package Client (if available)
# ============================================================================
print_section "Packaging Client"

CLIENT_DIR="$PROJECT_ROOT/src/client"
CLIENT_ARTIFACTS="$ARTIFACTS_DIR/client"

if [[ -d "$CLIENT_ARTIFACTS" ]]; then
    cp -r "$CLIENT_ARTIFACTS" "$PACKAGE_DIR/client"
    print_info "Copied client artifacts"
elif [[ -d "$CLIENT_DIR" ]]; then
    print_warning "Client artifacts not found. Run ./build.sh --package to build client."
    # Copy source as fallback
    mkdir -p "$PACKAGE_DIR/client-source"
    cp -r "$CLIENT_DIR"/* "$PACKAGE_DIR/client-source/" 2>/dev/null || true
    print_info "Copied client source as fallback"
else
    print_warning "Client directory not found"
fi

# ============================================================================
# Package Documentation
# ============================================================================
print_section "Packaging Documentation"

DOCS=("README.md" "LICENSE" "CHANGELOG.md" "CONTRIBUTING.md" "COMPILER_SETUP.md")
for doc in "${DOCS[@]}"; do
    if [[ -f "$PROJECT_ROOT/$doc" ]]; then
        cp "$PROJECT_ROOT/$doc" "$PACKAGE_DIR/"
        print_info "Copied: $doc"
    fi
done

# Create version info
print_info "Creating VERSION.txt"
cat > "$PACKAGE_DIR/VERSION.txt" << EOF
DarkAges MMO v$VERSION
Package Date: $(date '+%Y-%m-%d %H:%M:%S')
OS: $OS
EOF

# Create README for package
cat > "$PACKAGE_DIR/QUICKSTART.md" << 'EOF'
# DarkAges MMO - Quick Start Guide

## Server Setup

### Option 1: Direct Run
```bash
cd server
./darkages_server
```

### Option 2: Docker Compose
```bash
cd server
docker-compose up -d
```

## Client Setup

### Windows
Run `client/DarkAgesClient.exe`

### Linux
```bash
chmod +x client/DarkAgesClient.x86_64
./client/DarkAgesClient.x86_64
```

### macOS
Open `client/DarkAgesClient.app`

## Configuration

Server configuration files are located in:
- `server/config/` (if exists)
- Environment variables (see README.md)

## Support

For detailed documentation, see README.md
For build instructions, see COMPILER_SETUP.md
EOF

print_info "Created QUICKSTART.md"

# ============================================================================
# Create Archive
# ============================================================================
print_section "Creating Archive"

ARCHIVE_NAME="DarkAges-v$VERSION-$OS"

cd "$ARTIFACTS_DIR"

if [[ "$OS" == "win64" ]]; then
    # Windows - use zip
    ARCHIVE_FILE="${ARCHIVE_NAME}.zip"
    if command -v 7z &> /dev/null; then
        7z a -tzip "$ARCHIVE_FILE" "DarkAges-v$VERSION" 2>&1 | tail -5
    elif command -v zip &> /dev/null; then
        zip -r "$ARCHIVE_FILE" "DarkAges-v$VERSION" 2>&1 | tail -5
    else
        print_warning "No zip utility found, skipping archive creation"
        ARCHIVE_FILE=""
    fi
else
    # Unix - use tar.gz
    ARCHIVE_FILE="${ARCHIVE_NAME}.tar.gz"
    tar -czf "$ARCHIVE_FILE" "DarkAges-v$VERSION" 2>&1
fi

cd "$PROJECT_ROOT"

if [[ -n "$ARCHIVE_FILE" && -f "$ARTIFACTS_DIR/$ARCHIVE_FILE" ]]; then
    print_status "Archive created: $ARCHIVE_FILE"
    
    # Calculate checksum
    print_info "Calculating checksum..."
    if command -v sha256sum &> /dev/null; then
        sha256sum "$ARTIFACTS_DIR/$ARCHIVE_FILE" > "$ARTIFACTS_DIR/$ARCHIVE_FILE.sha256"
        CHECKSUM=$(sha256sum "$ARTIFACTS_DIR/$ARCHIVE_FILE" | awk '{print $1}')
    elif command -v shasum &> /dev/null; then
        shasum -a 256 "$ARTIFACTS_DIR/$ARCHIVE_FILE" > "$ARTIFACTS_DIR/$ARCHIVE_FILE.sha256"
        CHECKSUM=$(shasum -a 256 "$ARTIFACTS_DIR/$ARCHIVE_FILE" | awk '{print $1}')
    fi
    
    if [[ -n "$CHECKSUM" ]]; then
        print_info "SHA256: $CHECKSUM"
        print_info "Checksum file: $ARCHIVE_FILE.sha256"
    fi
    
    # Display file size
    SIZE=$(du -h "$ARTIFACTS_DIR/$ARCHIVE_FILE" | cut -f1)
    print_info "Size: $SIZE"
else
    print_warning "Archive creation may have failed"
fi

# ============================================================================
# Summary
# ============================================================================
print_section "Packaging Summary"

echo "Version: $VERSION"
echo "OS: $OS"
echo "Package Directory: $PACKAGE_DIR"
if [[ -n "$ARCHIVE_FILE" ]]; then
    echo "Archive: $ARTIFACTS_DIR/$ARCHIVE_FILE"
fi
echo ""

print_status "Packaging completed successfully!"
echo ""

# List package contents
echo "Package contents:"
find "$PACKAGE_DIR" -type f | head -20 | while read file; do
    rel_path="${file#$PACKAGE_DIR/}"
    echo "  $rel_path"
done

if [[ $(find "$PACKAGE_DIR" -type f | wc -l) -gt 20 ]]; then
    echo "  ... and more"
fi

echo ""
