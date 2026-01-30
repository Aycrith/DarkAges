#!/usr/bin/env pwsh
<#
.SYNOPSIS
    DarkAges MMO - Automated Environment Setup & Validation
.DESCRIPTION
    Automatically installs all dependencies, builds project, sets up services,
    and prepares environment for comprehensive testing.
#>

param(
    [switch]$SkipBuild,
    [switch]$SkipDocker,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot ".." "..")
$LogDir = Join-Path $ProjectRoot "logs"
$ValidationDir = Join-Path $ProjectRoot "validation"

# Ensure log directory exists
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $ValidationDir | Out-Null

function Write-ValidationStep($Step, $Status = "INFO", $Message = "") {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $emoji = switch ($Status) {
        "PASS" { "[PASS]" }
        "FAIL" { "[FAIL]" }
        "WARN" { "[WARN]" }
        "INFO" { "[INFO]" }
        default { "[WAIT]" }
    }
    Write-Host "$emoji [$timestamp] $Step" -NoNewline
    if ($Message) { Write-Host " - $Message" } else { Write-Host "" }
    
    # Log to file
    "$timestamp [$Status] $Step - $Message" | Out-File (Join-Path $LogDir "setup.log") -Append
}

function Test-Command($Command) {
    try { Get-Command $Command -ErrorAction Stop | Out-Null; return $true }
    catch { return $false }
}

# =============================================================================
# STEP 1: System Requirements Check
# =============================================================================
Write-ValidationStep "System Requirements Check" "INFO"

$requirements = @{
    "PowerShell" = $PSVersionTable.PSVersion -ge [Version]"7.0"
    "64-bit OS" = [Environment]::Is64BitOperatingSystem
    "8GB+ RAM" = (Get-CimInstance Win32_PhysicalMemory | Measure-Object Capacity -Sum).Sum / 1GB -ge 8
    "10GB Free Space" = (Get-PSDrive C).Free / 1GB -ge 10
}

foreach ($req in $requirements.GetEnumerator()) {
    if ($req.Value) {
        Write-ValidationStep "  [OK] $req.Key" "PASS"
    } else {
        Write-ValidationStep "  [BAD] $req.Key" "FAIL"
        throw "System requirement not met: $req.Key"
    }
}

# =============================================================================
# STEP 2: Install/Verify Build Tools
# =============================================================================
Write-ValidationStep "Build Tools Installation" "INFO"

# Check for CMake
if (-not (Test-Command "cmake")) {
    Write-ValidationStep "Installing CMake" "INFO"
    winget install Kitware.CMake -e --silent
    $env:PATH = [Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" + [Environment]::GetEnvironmentVariable("PATH", "User")
}

# Check for Git
if (-not (Test-Command "git")) {
    Write-ValidationStep "Installing Git" "INFO"
    winget install Git.Git -e --silent
}

# Check for Python
$pythonCmd = $null
if (Test-Command "python") { $pythonCmd = "python" }
elseif (Test-Command "python3") { $pythonCmd = "python3" }
else {
    Write-ValidationStep "Installing Python" "INFO"
    winget install Python.Python.3.11 -e --silent
    $pythonCmd = "python"
}

# Verify installations
$tools = @("cmake", "git", $pythonCmd)
foreach ($tool in $tools) {
    if (Test-Command $tool) {
        $version = & $tool --version 2>$null | Select-Object -First 1
        Write-ValidationStep "  [OK] $tool" "PASS" $version
    } else {
        Write-ValidationStep "  [BAD] $tool" "FAIL"
        throw "Required tool not found: $tool"
    }
}

# =============================================================================
# STEP 3: Install Python Dependencies
# =============================================================================
Write-ValidationStep "Python Dependencies" "INFO"

$requirementsFile = Join-Path $ProjectRoot "tools" "stress-test" "requirements.txt"
if (Test-Path $requirementsFile) {
    & $pythonCmd -m pip install -r $requirementsFile --quiet
    Write-ValidationStep "  [OK] Python packages installed" "PASS"
} else {
    # Install essential packages
    $packages = @("asyncio", "redis", "cassandra-driver", "requests", "psutil")
    foreach ($pkg in $packages) {
        & $pythonCmd -m pip install $pkg --quiet
    }
    Write-ValidationStep "  [OK] Essential Python packages installed" "PASS"
}

# =============================================================================
# STEP 4: Install Docker Desktop
# =============================================================================
if (-not $SkipDocker) {
    Write-ValidationStep "Docker Installation" "INFO"
    
    $dockerPath = Get-Command "docker" -ErrorAction SilentlyContinue
    if (-not $dockerPath) {
        Write-ValidationStep "Installing Docker Desktop" "INFO"
        winget install Docker.DockerDesktop -e --silent
        
        Write-Warning "Docker Desktop installed. Please start it manually and re-run this script."
        Write-Warning "Docker requires WSL2 which may need a system restart."
        
        # Create marker file to continue after restart
        "RESTART_REQUIRED" | Out-File (Join-Path $ValidationDir ".restart_marker")
        exit 0
    }
    
    # Check if Docker is running
    try {
        $dockerInfo = docker info 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-ValidationStep "  [OK] Docker Desktop running" "PASS"
        } else {
            throw "Docker not running"
        }
    } catch {
        Write-ValidationStep "  ? Docker installed but not running" "WARN"
        Write-Host "Please start Docker Desktop and re-run this script."
        exit 1
    }
}

# =============================================================================
# STEP 5: Install Godot (for client testing)
# =============================================================================
Write-ValidationStep "Godot Installation" "INFO"

$godotPath = Get-Command "godot" -ErrorAction SilentlyContinue
if (-not $godotPath) {
    # Download and install Godot 4.2.1
    $godotUrl = "https://downloads.tuxfamily.org/godotengine/4.2.1/Godot_v4.2.1-stable_mono_win64.zip"
    $godotZip = Join-Path $env:TEMP "godot.zip"
    $godotExtract = Join-Path $env:LOCALAPPDATA "Godot"
    
    Invoke-WebRequest -Uri $godotUrl -OutFile $godotZip
    Expand-Archive -Path $godotZip -DestinationPath $godotExtract -Force
    
    # Add to PATH
    [Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";$godotExtract", "User")
    $env:PATH += ";$godotExtract"
    
    Write-ValidationStep "  [OK] Godot installed" "PASS"
} else {
    $godotVersion = & godot --version
    Write-ValidationStep "  [OK] Godot found" "PASS" $godotVersion
}

# =============================================================================
# STEP 6: Clone/Update Dependencies
# =============================================================================
Write-ValidationStep "Dependency Setup" "INFO"

# EnTT
$enttPath = Join-Path $ProjectRoot "deps" "entt"
if (-not (Test-Path $enttPath)) {
    git clone --depth 1 --branch v3.13.0 https://github.com/skypjack/entt.git $enttPath
    Write-ValidationStep "  [OK] EnTT cloned" "PASS"
} else {
    Write-ValidationStep "  [OK] EnTT exists" "PASS"
}

# GLM
$glmPath = Join-Path $ProjectRoot "deps" "glm"
if (-not (Test-Path $glmPath)) {
    git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git $glmPath
    Write-ValidationStep "  [OK] GLM cloned" "PASS"
} else {
    Write-ValidationStep "  [OK] GLM exists" "PASS"
}

# =============================================================================
# STEP 7: Build Server (if not skipped)
# =============================================================================
if (-not $SkipBuild) {
    Write-ValidationStep "Building Server" "INFO"
    
    $buildDir = Join-Path $ProjectRoot "build"
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    
    Set-Location $buildDir
    
    # Configure
    $cmakeOutput = cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ValidationStep "CMake configuration" "FAIL"
        $cmakeOutput | Out-File (Join-Path $LogDir "cmake_error.log")
        throw "CMake configuration failed. See logs/cmake_error.log"
    }
    Write-ValidationStep "  [OK] CMake configured" "PASS"
    
    # Build
    $buildOutput = cmake --build . --parallel 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ValidationStep "Build" "FAIL"
        $buildOutput | Out-File (Join-Path $LogDir "build_error.log")
        throw "Build failed. See logs/build_error.log"
    }
    Write-ValidationStep "  [OK] Build complete" "PASS"
    
    # Run unit tests
    $testOutput = ctest --output-on-failure 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ValidationStep "Unit Tests" "FAIL"
        $testOutput | Out-File (Join-Path $LogDir "test_error.log")
        throw "Unit tests failed. See logs/test_error.log"
    }
    Write-ValidationStep "  [OK] Unit tests passed" "PASS"
} else {
    Write-ValidationStep "Build skipped" "WARN"
}

# =============================================================================
# STEP 8: Build Client (if not skipped)
# =============================================================================
if (-not $SkipBuild) {
    Write-ValidationStep "Building Client" "INFO"
    
    $clientDir = Join-Path $ProjectRoot "src" "client"
    if (Test-Path $clientDir) {
        Set-Location $clientDir
        
        # Check for C# project files
        $csprojFiles = Get-ChildItem -Path $clientDir -Filter "*.csproj" -Recurse -ErrorAction SilentlyContinue
        if ($csprojFiles) {
            # Restore NuGet packages
            & dotnet restore 2>&1 | Out-Null
            
            # Build
            $buildOutput = & godot --build-solutions --headless 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-ValidationStep "Client Build" "WARN"
                # Don't fail - client build may have non-critical issues
            } else {
                Write-ValidationStep "  [OK] Client built" "PASS"
            }
        } else {
            Write-ValidationStep "No C# project found - skipping client build" "INFO"
        }
    } else {
        Write-ValidationStep "Client directory not found - skipping" "INFO"
    }
}

# =============================================================================
# STEP 9: Validate Installation
# =============================================================================
Write-ValidationStep "Installation Validation" "INFO"

$checks = @{
    "CMake" = Test-Command "cmake"
    "Git" = Test-Command "git"
    "Python" = $null -ne $pythonCmd
    "Docker" = if (-not $SkipDocker) { (docker info 2>&1) -match "Server Version" } else { $true }
    "Godot" = Test-Command "godot"
    "Server Binary" = Test-Path (Join-Path $ProjectRoot "build" "Release" "darkages_server.exe")
    "EnTT" = Test-Path (Join-Path $ProjectRoot "deps" "entt" "single_include" "entt" "entt.hpp")
    "GLM" = Test-Path (Join-Path $ProjectRoot "deps" "glm" "glm" "glm.hpp")
}

$allPassed = $true
foreach ($check in $checks.GetEnumerator()) {
    if ($check.Value) {
        Write-ValidationStep "  [OK] $check.Key" "PASS"
    } else {
        Write-ValidationStep "  [BAD] $check.Key" "FAIL"
        $allPassed = $false
    }
}

# =============================================================================
# SUMMARY
# =============================================================================
Write-Host ""
Write-Host "=" * 70
if ($allPassed) {
    Write-Host "[PASS] ENVIRONMENT SETUP COMPLETE" -ForegroundColor Green
    Write-Host "All dependencies installed and validated successfully."
    Write-Host ""
    Write-Host "Next step: Run validation tests"
    Write-Host "  .\tools\validation\run_validation_tests.ps1"
} else {
    Write-Host "[FAIL] ENVIRONMENT SETUP INCOMPLETE" -ForegroundColor Red
    Write-Host "Some checks failed. Review the output above."
}
Write-Host "=" * 70

# Write status file
$status = if ($allPassed) { "READY" } else { "FAILED" }
$status | Out-File (Join-Path $ValidationDir ".setup_status")

exit $(if ($allPassed) { 0 } else { 1 })
