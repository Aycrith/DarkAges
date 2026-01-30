#!/usr/bin/env powershell
# DarkAges MMO - Quick Start Script
# One-command setup and build for new developers

param(
    [switch]$SkipCompilerCheck,
    [switch]$SkipBuild,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

Write-Host @"
========================================
  DarkAges MMO - Quick Start
========================================
"@ -ForegroundColor Cyan

# Step 1: Check system
Write-Host "[Step 1/4] Checking system..." -ForegroundColor Yellow

# Check Windows version
$osInfo = Get-CimInstance Win32_OperatingSystem
Write-Host "  OS: $($osInfo.Caption)" -ForegroundColor Gray

# Check PowerShell version
if ($PSVersionTable.PSVersion.Major -lt 5) {
    Write-Error "PowerShell 5.0 or later required"
}
Write-Host "  PowerShell: $($PSVersionTable.PSVersion)" -ForegroundColor Gray

# Check for Git
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Error "Git not found. Please install Git from https://git-scm.com/"
}
Write-Host "  Git: OK" -ForegroundColor Gray

# Step 2: Check/Install Compiler
if (-not $SkipCompilerCheck) {
    Write-Host ""
    Write-Host "[Step 2/4] Checking for C++ compiler..." -ForegroundColor Yellow
    
    $hasCompiler = $false
    
    # Check for any compiler
    if (Get-Command cl -ErrorAction SilentlyContinue) {
        Write-Host "  Found: MSVC" -ForegroundColor Green
        $hasCompiler = $true
    }
    elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
        Write-Host "  Found: Clang" -ForegroundColor Green
        $hasCompiler = $true
    }
    elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
        Write-Host "  Found: GCC/MinGW" -ForegroundColor Green
        $hasCompiler = $true
    }
    
    if (-not $hasCompiler) {
        Write-Host "  No compiler found!" -ForegroundColor Red
        Write-Host ""
        Write-Host "Options:" -ForegroundColor White
        Write-Host "  1. Install MSVC (Recommended)" -ForegroundColor Yellow
        Write-Host "     .\install_msvc.ps1" -ForegroundColor Gray
        Write-Host ""
        Write-Host "  2. Install Clang" -ForegroundColor Yellow
        Write-Host "     winget install LLVM.LLVM" -ForegroundColor Gray
        Write-Host ""
        Write-Host "  3. Install MinGW" -ForegroundColor Yellow
        Write-Host "     Install MSYS2 from https://www.msys2.org/" -ForegroundColor Gray
        Write-Host ""
        
        $response = Read-Host "Would you like to install MSVC now? (Y/n)"
        if ($response -eq '' -or $response -eq 'y' -or $response -eq 'Y') {
            .\install_msvc.ps1
            # Re-run this script after installation
            Write-Host ""
            Write-Host "Please restart PowerShell and run quickstart.ps1 again" -ForegroundColor Yellow
            exit 0
        } else {
            Write-Error "Cannot continue without a C++ compiler"
        }
    }
} else {
    Write-Host ""
    Write-Host "[Step 2/4] Skipping compiler check" -ForegroundColor Yellow
}

# Step 3: Setup Environment
Write-Host ""
Write-Host "[Step 3/4] Setting up build environment..." -ForegroundColor Yellow

if (Test-Path ".\setup_compiler.ps1") {
    .\setup_compiler.ps1
} else {
    Write-Warning "setup_compiler.ps1 not found, attempting to continue..."
}

# Step 4: Build
if (-not $SkipBuild) {
    Write-Host ""
    Write-Host "[Step 4/4] Building project..." -ForegroundColor Yellow
    
    $buildArgs = @("-Test")
    if ($Verbose) {
        $buildArgs += "-Verbose"
    }
    
    .\build.ps1 @buildArgs
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "  Setup Complete!" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Next steps:" -ForegroundColor White
        Write-Host "  - Run server: .\build\Release\darkages_server.exe" -ForegroundColor Yellow
        Write-Host "  - Run tests: cd build && ctest" -ForegroundColor Yellow
        Write-Host "  - Read docs: docs\IMPLEMENTATION_SUMMARY.md" -ForegroundColor Yellow
        Write-Host ""
    } else {
        Write-Error "Build failed. Check the output above for errors."
    }
} else {
    Write-Host ""
    Write-Host "[Step 4/4] Skipping build" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To build manually, run: .\build.ps1 -Test" -ForegroundColor Yellow
    Write-Host ""
}
