# ==============================================================================
# DarkAges MMO - Redis Integration Testing Script (Windows)
# ==============================================================================
# 
# Quick script to start Redis via Docker and run integration tests
#
# Usage:
#   .\scripts\test-with-redis.ps1                    # Run all tests (excluding pub/sub)
#   .\scripts\test-with-redis.ps1 -RedisOnly         # Run only Redis tests
#   .\scripts\test-with-redis.ps1 -KeepRedis         # Don't stop Redis after tests
#   .\scripts\test-with-redis.ps1 -JUnitReport       # Generate JUnit XML report
#
# ==============================================================================

param(
    [switch]$RedisOnly,
    [switch]$KeepRedis,
    [switch]$JUnitReport,
    [string]$TestFilter = "~*Pub/Sub*"
)

# Colors for output
function Write-Status($message) {
    Write-Host "[+] $message" -ForegroundColor Green
}

function Write-Info($message) {
    Write-Host "[>] $message" -ForegroundColor Cyan
}

function Write-Warning($message) {
    Write-Host "[!] $message" -ForegroundColor Yellow
}

function Write-Error-Custom($message) {
    Write-Host "[X] $message" -ForegroundColor Red
}

Write-Status "DarkAges Redis Testing Script"
Write-Info "Starting Redis integration tests..."
Write-Host ""

# ==============================================================================
# Check Prerequisites
# ==============================================================================

Write-Info "Checking prerequisites..."

# Check Docker
try {
    $dockerVersion = docker --version 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker not found"
    }
    Write-Status "Docker: $dockerVersion"
} catch {
    Write-Error-Custom "Docker is not installed or not running"
    Write-Info "Please install Docker Desktop and ensure it's running"
    exit 1
}

# Check docker-compose
try {
    $composeVersion = docker compose version 2>$null
    if ($LASTEXITCODE -ne 0) {
        $composeVersion = docker-compose --version 2>$null
    }
    Write-Status "Docker Compose: $composeVersion"
} catch {
    Write-Error-Custom "Docker Compose not found"
    exit 1
}

# Check test executable
$testExe = "build\Release\darkages_tests.exe"
if (-not (Test-Path $testExe)) {
    Write-Error-Custom "Test executable not found: $testExe"
    Write-Info "Please build the project first:"
    Write-Info "  cmake -B build -DBUILD_TESTS=ON"
    Write-Info "  cmake --build build --config Release"
    exit 1
}
Write-Status "Test executable found"

Write-Host ""

# ==============================================================================
# Start Redis
# ==============================================================================

Write-Status "Starting Redis container..."

try {
    # Start Redis using docker-compose
    docker-compose -f infra/docker-compose.redis.yml up -d 2>&1 | Out-Null
    
    # Wait for Redis to be healthy
    Write-Info "Waiting for Redis to be ready..."
    $maxWait = 30
    $elapsed = 0
    $ready = $false
    
    while ($elapsed -lt $maxWait) {
        try {
            $health = docker inspect darkages-redis --format='{{.State.Health.Status}}' 2>$null
            if ($health -eq "healthy") {
                $ready = $true
                break
            }
        } catch {
            # Container might not be fully started yet
        }
        
        Start-Sleep -Seconds 1
        $elapsed++
        Write-Host "." -NoNewline
    }
    
    Write-Host ""
    
    if (-not $ready) {
        Write-Warning "Redis health check timed out, but continuing anyway..."
    } else {
        Write-Status "Redis is ready!"
    }
    
    # Verify Redis is responding
    try {
        $pong = docker exec darkages-redis redis-cli ping 2>$null
        if ($pong -eq "PONG") {
            Write-Status "Redis connectivity verified (PONG received)"
        }
    } catch {
        Write-Warning "Could not verify Redis connectivity, but continuing..."
    }
    
} catch {
    Write-Error-Custom "Failed to start Redis: $_"
    exit 1
}

Write-Host ""

# ==============================================================================
# Run Tests
# ==============================================================================

Write-Status "Running tests..."
Write-Host ""

try {
    # Determine test filter
    if ($RedisOnly) {
        $filter = "[redis]"
        Write-Info "Running Redis integration tests only"
    } else {
        $filter = $TestFilter
        Write-Info "Running full test suite (excluding $TestFilter)"
    }
    
    # Build test command
    $testArgs = @($filter, "--reporter", "console")
    
    if ($JUnitReport) {
        $testArgs += @("--out", "build/test_results.xml", "--reporter", "junit")
        Write-Info "JUnit report will be saved to: build/test_results.xml"
    }
    
    # Run tests
    Push-Location build
    try {
        & ".\Release\darkages_tests.exe" $testArgs
        $testResult = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    
    Write-Host ""
    
    if ($testResult -eq 0) {
        Write-Status "All tests passed! ✓"
    } else {
        Write-Error-Custom "Some tests failed (exit code: $testResult)"
    }
    
} catch {
    Write-Error-Custom "Failed to run tests: $_"
    $testResult = 1
}

# ==============================================================================
# Cleanup
# ==============================================================================

if (-not $KeepRedis) {
    Write-Host ""
    Write-Info "Stopping Redis container..."
    try {
        docker-compose -f infra/docker-compose.redis.yml down 2>&1 | Out-Null
        Write-Status "Redis stopped"
    } catch {
        Write-Warning "Could not stop Redis: $_"
    }
} else {
    Write-Host ""
    Write-Info "Redis container left running (use -KeepRedis to stop manually)"
    Write-Info "To stop Redis: docker-compose -f infra/docker-compose.redis.yml down"
}

Write-Host ""
Write-Status "Testing complete!"

exit $testResult
