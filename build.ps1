# DarkAges MMO Server - Build Script (PowerShell)
# This script builds the server and runs tests

param(
    [string]$BuildType = "Release",
    [switch]$Clean,
    [switch]$Test,
    [switch]$Verbose,
    [switch]$SetupCompiler,
    [ValidateSet("Auto", "MSVC", "Clang", "MinGW")]
    [string]$Compiler = "Auto"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DarkAges Server Build" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if we need to set up the compiler
if ($SetupCompiler -or $Compiler -ne "Auto") {
    if (Test-Path ".\setup_compiler.ps1") {
        .\setup_compiler.ps1 -Compiler $Compiler
    } else {
        Write-Warning "setup_compiler.ps1 not found. Attempting to build with system default compiler."
    }
    Write-Host ""
}

# Check prerequisites
Write-Host "[1/5] Checking prerequisites..." -ForegroundColor Yellow

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Error "CMake not found. Please install CMake 3.20 or later."
    exit 1
}
Write-Host "  CMake: $(cmake --version | Select-Object -First 1)" -ForegroundColor Green

# Check for compiler
$hasCompiler = $false
$compilerName = "Unknown"

# Check for MSVC
if (Get-Command cl -ErrorAction SilentlyContinue) {
    $hasCompiler = $true
    $compilerName = "MSVC"
    Write-Host "  Compiler: MSVC ($(cl 2>&1 | Select-Object -First 1))" -ForegroundColor Green
}
# Check for Clang
elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
    $hasCompiler = $true
    $compilerName = "Clang"
    Write-Host "  Compiler: Clang ($(clang++ --version | Select-Object -First 1))" -ForegroundColor Green
}
# Check for GCC
elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    $hasCompiler = $true
    $compilerName = "GCC"
    Write-Host "  Compiler: GCC ($(g++ --version | Select-Object -First 1))" -ForegroundColor Green
}

if (-not $hasCompiler) {
    Write-Host ""
    Write-Warning "No C++ compiler detected!"
    Write-Host ""
    Write-Host "Please run: .\setup_compiler.ps1" -ForegroundColor Yellow
    Write-Host "Or install a compiler manually (see COMPILER_SETUP.md)" -ForegroundColor Gray
    Write-Host ""
    
    $response = Read-Host "Attempt to build anyway? (y/N)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        exit 1
    }
}

# Check dependencies
Write-Host "  Checking dependencies..." -ForegroundColor Yellow

$depsOk = $true

if (-not (Test-Path "deps/entt/single_include/entt/entt.hpp")) {
    Write-Warning "  EnTT not found in deps/entt"
    $depsOk = $false
} else {
    Write-Host "  EnTT: OK" -ForegroundColor Green
}

if (-not (Test-Path "deps/glm/glm/glm.hpp")) {
    Write-Warning "  GLM not found in deps/glm"
    $depsOk = $false
} else {
    Write-Host "  GLM: OK" -ForegroundColor Green
}

if (-not $depsOk) {
    Write-Host ""
    Write-Host "Attempting to fetch missing dependencies..." -ForegroundColor Yellow
    
    if (-not (Test-Path "deps/entt")) {
        Write-Host "  Cloning EnTT..."
        git clone --depth 1 --branch v3.13.0 https://github.com/skypjack/entt.git deps/entt 2>&1 | Out-Null
    }
    
    if (-not (Test-Path "deps/glm")) {
        Write-Host "  Cloning GLM..."
        git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git deps/glm 2>&1 | Out-Null
    }
}

# Clean if requested
if ($Clean) {
    Write-Host ""
    Write-Host "[2/5] Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path "build") {
        Remove-Item -Recurse -Force "build"
        Write-Host "  Cleaned build directory" -ForegroundColor Green
    }
} else {
    Write-Host ""
    Write-Host "[2/5] Skipping clean (use -Clean to force)" -ForegroundColor Yellow
}

# Create build directory
Write-Host ""
Write-Host "[3/5] Configuring CMake..." -ForegroundColor Yellow

New-Item -ItemType Directory -Force -Path "build" | Out-Null
Set-Location "build"

# Determine generator based on compiler
$cmakeArgs = @(
    "..",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DBUILD_TESTS=ON"
)

if ($compilerName -eq "MSVC") {
    # Use Ninja if available for faster builds
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($ninja) {
        $cmakeArgs += "-GNinja"
        Write-Host "  Using Ninja generator" -ForegroundColor Gray
    } else {
        # Let CMake choose (will use Visual Studio generator on Windows)
        Write-Host "  Using default generator for MSVC" -ForegroundColor Gray
    }
} elseif ($compilerName -eq "MinGW") {
    $cmakeArgs += "-GMinGW Makefiles"
    Write-Host "  Using MinGW Makefiles generator" -ForegroundColor Gray
}

if ($Verbose) {
    $cmakeArgs += "-DCMAKE_VERBOSE_MAKEFILE=ON"
}

try {
    & cmake @cmakeArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }
} catch {
    Write-Error "CMake configuration failed: $_"
    Set-Location ".."
    exit 1
}

Write-Host "  CMake configuration successful" -ForegroundColor Green

# Build
Write-Host ""
Write-Host "[4/5] Building..." -ForegroundColor Yellow
Write-Host "  Build type: $BuildType" -ForegroundColor Gray
Write-Host "  Compiler: $compilerName" -ForegroundColor Gray
Write-Host ""

try {
    $buildArgs = @("--build", ".", "--config", $BuildType)
    
    # Add parallel build
    if ($compilerName -eq "MSVC" -and -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        # Visual Studio generator handles parallel internally
    } else {
        $buildArgs += "--parallel"
    }
    
    if ($Verbose) {
        $buildArgs += "--verbose"
    }
    
    & cmake @buildArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
} catch {
    Write-Error "Build failed: $_"
    Set-Location ".."
    exit 1
}

Write-Host "  Build successful" -ForegroundColor Green

# Test
if ($Test) {
    Write-Host ""
    Write-Host "[5/5] Running tests..." -ForegroundColor Yellow
    
    try {
        & ctest -C $BuildType --output-on-failure 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed"
        }
        Write-Host "  All tests passed" -ForegroundColor Green
    } catch {
        Write-Error "Tests failed: $_"
        Set-Location ".."
        exit 1
    }
} else {
    Write-Host ""
    Write-Host "[5/5] Skipping tests (use -Test to run)" -ForegroundColor Yellow
}

Set-Location ".."

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Build Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Find the built executables
$exePaths = @(
    "build/$BuildType/darkages_server.exe",
    "build/darkages_server.exe",
    "build/darkages_server",
    "build/Release/darkages_server.exe",
    "build/Release/darkages_server"
)

Write-Host "Executables:" -ForegroundColor White
$foundExe = $false
foreach ($path in $exePaths) {
    if (Test-Path $path) {
        Write-Host "  $path" -ForegroundColor Gray
        $foundExe = $true
    }
}

if (-not $foundExe) {
    Write-Host "  (Check the build directory)" -ForegroundColor Gray
}

Write-Host ""

# Summary
if ($Test -and $foundExe) {
    Write-Host "Summary:" -ForegroundColor White
    Write-Host "  Build: SUCCESS" -ForegroundColor Green
    Write-Host "  Tests: PASSED" -ForegroundColor Green
    Write-Host "  Status: Ready for use!" -ForegroundColor Green
} elseif ($foundExe) {
    Write-Host "Summary:" -ForegroundColor White
    Write-Host "  Build: SUCCESS" -ForegroundColor Green
    Write-Host "  Tests: Not run (use -Test next time)" -ForegroundColor Yellow
}

Write-Host ""
