#!/bin/bash
# ============================================================================
# DarkAges MMO - Docker Build Helper Script
# ============================================================================
# Simplifies building and running Docker images
#
# Usage:
#   ./tools/build/docker_build.sh [command] [options]
#
# Commands:
#   build           Build Docker image (default)
#   run             Run Docker container
#   test            Run tests in Docker
#   shell           Start interactive shell in container
#   clean           Remove Docker images and containers
#
# Options:
#   -t, --tag       Image tag (default: darkages-server:latest)
#   -v, --version   Version tag
#   --no-cache      Build without cache
#   --push          Push to registry after build
#
# Examples:
#   ./tools/build/docker_build.sh build
#   ./tools/build/docker_build.sh build --no-cache -t myrepo/darkages:v1.0
#   ./tools/build/docker_build.sh run -p 7777:7777
#   ./tools/build/docker_build.sh test
#   ./tools/build/docker_build.sh shell
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
VERSION_FILE="$PROJECT_ROOT/VERSION"

# Defaults
COMMAND="build"
IMAGE_TAG="darkages-server:latest"
DOCKERFILE="$PROJECT_ROOT/Dockerfile.build"
BUILD_TARGET="runtime"
NO_CACHE=""
PUSH=false
RUN_ARGS=""

# Get version
if [[ -f "$VERSION_FILE" ]]; then
    VERSION=$(cat "$VERSION_FILE" | tr -d '[:space:]')
else
    VERSION="0.1.0"
fi

# Parse command
if [[ $# -gt 0 && ! "$1" =~ ^- ]]; then
    COMMAND="$1"
    shift
fi

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--tag)
            IMAGE_TAG="$2"
            shift 2
            ;;
        -v|--version)
            VERSION="$2"
            shift 2
            ;;
        --no-cache)
            NO_CACHE="--no-cache"
            shift
            ;;
        --push)
            PUSH=true
            shift
            ;;
        -p|--port)
            RUN_ARGS="$RUN_ARGS -p $2"
            shift 2
            ;;
        -d|--detach)
            RUN_ARGS="$RUN_ARGS -d"
            shift
            ;;
        --target)
            BUILD_TARGET="$2"
            shift 2
            ;;
        -h|--help)
            head -n 35 "$0" | tail -n 33
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Ensure Docker is available
if ! command -v docker &> /dev/null; then
    print_error "Docker not found. Please install Docker."
    exit 1
fi

print_section "DarkAges MMO Docker Build"
print_info "Command: $COMMAND"
print_info "Image: $IMAGE_TAG"
print_info "Version: $VERSION"
print_info "Target: $BUILD_TARGET"

# Execute command
case $COMMAND in
    build)
        print_section "Building Docker Image"
        
        BUILD_ARGS=(
            "-f" "$DOCKERFILE"
            "--target" "$BUILD_TARGET"
            "-t" "$IMAGE_TAG"
            "$NO_CACHE"
        )
        
        # Add version build arg
        BUILD_ARGS+=("--build-arg" "VERSION=$VERSION")
        
        print_info "Running: docker build ${BUILD_ARGS[*]} $PROJECT_ROOT"
        docker build "${BUILD_ARGS[@]}" "$PROJECT_ROOT"
        
        print_status "Build complete: $IMAGE_TAG"
        
        if [[ "$PUSH" == true ]]; then
            print_info "Pushing to registry..."
            docker push "$IMAGE_TAG"
        fi
        ;;
    
    run)
        print_section "Running Docker Container"
        
        # Default port mapping if not specified
        if [[ ! "$RUN_ARGS" =~ "-p" ]]; then
            RUN_ARGS="$RUN_ARGS -p 7777:7777/udp -p 7777:7777/tcp"
        fi
        
        print_info "Running: docker run $RUN_ARGS $IMAGE_TAG"
        docker run --rm $RUN_ARGS "$IMAGE_TAG"
        ;;
    
    test)
        print_section "Running Tests in Docker"
        
        # Build test image
        TEST_TAG="darkages-server:test"
        print_info "Building test image..."
        docker build \
            -f "$DOCKERFILE" \
            --target ci-test \
            -t "$TEST_TAG" \
            "$PROJECT_ROOT"
        
        # Run tests
        print_info "Running tests..."
        docker run --rm "$TEST_TAG"
        
        print_status "Tests completed"
        ;;
    
    shell)
        print_section "Starting Interactive Shell"
        
        # Build dev image if needed
        DEV_TAG="darkages-server:dev"
        if [[ -z "$(docker images -q $DEV_TAG 2>/dev/null)" ]]; then
            print_info "Building development image..."
            docker build \
                -f "$DOCKERFILE" \
                --target development \
                -t "$DEV_TAG" \
                "$PROJECT_ROOT"
        fi
        
        print_info "Starting shell in container..."
        docker run --rm -it \
            -v "$PROJECT_ROOT:/build" \
            -w /build \
            "$DEV_TAG" \
            /bin/bash
        ;;
    
    clean)
        print_section "Cleaning Docker Images"
        
        print_warning "Removing DarkAges Docker images..."
        docker rmi -f darkages-server:latest darkages-server:test darkages-server:dev 2>/dev/null || true
        
        print_warning "Removing dangling images..."
        docker image prune -f 2>/dev/null || true
        
        print_status "Cleanup complete"
        ;;
    
    compose)
        print_section "Docker Compose Operations"
        
        cd "$PROJECT_ROOT/infra"
        
        if [[ -f "docker-compose.yml" ]]; then
            print_info "Starting services with docker-compose..."
            docker-compose up -d --build
            print_status "Services started"
            print_info "Use 'docker-compose logs -f' to view logs"
        else
            print_error "docker-compose.yml not found in infra/"
            exit 1
        fi
        ;;
    
    *)
        print_error "Unknown command: $COMMAND"
        print_info "Valid commands: build, run, test, shell, clean, compose"
        exit 1
        ;;
esac

print_section "Done"
