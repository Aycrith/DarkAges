#!/bin/bash
# ==============================================================================
# DarkAges MMO - Redis Integration Testing Script (Linux/macOS)
# ==============================================================================
# 
# Quick script to start Redis via Docker and run integration tests
#
# Usage:
#   ./scripts/test-with-redis.sh                    # Run all tests (excluding pub/sub)
#   ./scripts/test-with-redis.sh --redis-only       # Run only Redis tests
#   ./scripts/test-with-redis.sh --keep-redis       # Don't stop Redis after tests
#   ./scripts/test-with-redis.sh --junit            # Generate JUnit XML report
#
# ==============================================================================

set -e

# Parse arguments
REDIS_ONLY=false
KEEP_REDIS=false
JUNIT_REPORT=false
TEST_FILTER="~*Pub/Sub*"

while [[ $# -gt 0 ]]; do
    case $1 in
        --redis-only)
            REDIS_ONLY=true
            shift
            ;;
        --keep-redis)
            KEEP_REDIS=true
            shift
            ;;
        --junit)
            JUNIT_REPORT=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--redis-only] [--keep-redis] [--junit]"
            exit 1
            ;;
    esac
done

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${GREEN}[+]${NC} $1"
}

print_info() {
    echo -e "${CYAN}[>]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[X]${NC} $1"
}

print_status "DarkAges Redis Testing Script"
print_info "Starting Redis integration tests..."
echo ""

# ==============================================================================
# Check Prerequisites
# ==============================================================================

print_info "Checking prerequisites..."

# Check Docker
if ! command -v docker &> /dev/null; then
    print_error "Docker is not installed or not in PATH"
    print_info "Please install Docker: https://docs.docker.com/get-docker/"
    exit 1
fi
print_status "Docker: $(docker --version)"

# Check docker-compose
if command -v docker-compose &> /dev/null; then
    COMPOSE_CMD="docker-compose"
    print_status "Docker Compose: $(docker-compose --version)"
elif docker compose version &> /dev/null; then
    COMPOSE_CMD="docker compose"
    print_status "Docker Compose: $(docker compose version)"
else
    print_error "Docker Compose not found"
    exit 1
fi

# Check test executable
if [[ -f "build/darkages_tests" ]]; then
    TEST_EXE="build/darkages_tests"
elif [[ -f "build/Release/darkages_tests.exe" ]]; then
    TEST_EXE="build/Release/darkages_tests.exe"
else
    print_error "Test executable not found"
    print_info "Please build the project first:"
    print_info "  cmake -B build -DBUILD_TESTS=ON"
    print_info "  cmake --build build --config Release"
    exit 1
fi
print_status "Test executable found: $TEST_EXE"

echo ""

# ==============================================================================
# Start Redis
# ==============================================================================

print_status "Starting Redis container..."

# Start Redis using docker-compose
$COMPOSE_CMD -f infra/docker-compose.redis.yml up -d > /dev/null 2>&1

# Wait for Redis to be healthy
print_info "Waiting for Redis to be ready..."
max_wait=30
elapsed=0
ready=false

while [[ $elapsed -lt $max_wait ]]; do
    health=$(docker inspect darkages-redis --format='{{.State.Health.Status}}' 2>/dev/null || echo "starting")
    if [[ "$health" == "healthy" ]]; then
        ready=true
        break
    fi
    
    sleep 1
    elapsed=$((elapsed + 1))
    echo -n "."
done

echo ""

if [[ "$ready" == "false" ]]; then
    print_warning "Redis health check timed out, but continuing anyway..."
else
    print_status "Redis is ready!"
fi

# Verify Redis is responding
if pong=$(docker exec darkages-redis redis-cli ping 2>/dev/null); then
    if [[ "$pong" == "PONG" ]]; then
        print_status "Redis connectivity verified (PONG received)"
    fi
fi

echo ""

# ==============================================================================
# Run Tests
# ==============================================================================

print_status "Running tests..."
echo ""

# Determine test filter
if [[ "$REDIS_ONLY" == "true" ]]; then
    filter="[redis]"
    print_info "Running Redis integration tests only"
else
    filter="$TEST_FILTER"
    print_info "Running full test suite (excluding $TEST_FILTER)"
fi

# Build test command
test_args=("$filter" "--reporter" "console")

if [[ "$JUNIT_REPORT" == "true" ]]; then
    test_args+=("--out" "build/test_results.xml" "--reporter" "junit")
    print_info "JUnit report will be saved to: build/test_results.xml"
fi

# Run tests
cd build
set +e  # Don't exit on test failure
"$TEST_EXE" "${test_args[@]}"
test_result=$?
set -e
cd ..

echo ""

if [[ $test_result -eq 0 ]]; then
    print_status "All tests passed! ✓"
else
    print_error "Some tests failed (exit code: $test_result)"
fi

# ==============================================================================
# Cleanup
# ==============================================================================

if [[ "$KEEP_REDIS" == "false" ]]; then
    echo ""
    print_info "Stopping Redis container..."
    $COMPOSE_CMD -f infra/docker-compose.redis.yml down > /dev/null 2>&1
    print_status "Redis stopped"
else
    echo ""
    print_info "Redis container left running"
    print_info "To stop Redis: $COMPOSE_CMD -f infra/docker-compose.redis.yml down"
fi

echo ""
print_status "Testing complete!"

exit $test_result
