#!/usr/bin/env powershell
# DarkAges MMO - MSVC (Visual Studio Build Tools) Installer
# This script downloads and installs the Visual C++ compiler

param(
    [switch]$WaitForCompletion
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MSVC (Visual Studio Build Tools) Installer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if already installed
Write-Host "Checking for existing installation..." -ForegroundColor Yellow
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (Test-Path $vswhere) {
    $existing = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 2>$null
    if ($existing) {
        Write-Host ""
        Write-Host "MSVC is already installed!" -ForegroundColor Green
        Write-Host "Location: $existing" -ForegroundColor Gray
        Write-Host ""
        Write-Host "Run 'setup_compiler.ps1' to configure your environment." -ForegroundColor Yellow
        exit 0
    }
}

# Download installer
$installerUrl = "https://aka.ms/vs/17/release/vs_buildtools.exe"
$installerPath = "$env:TEMP\vs_buildtools.exe"

Write-Host "Downloading Visual Studio Build Tools..." -ForegroundColor Yellow
Write-Host "  From: $installerUrl" -ForegroundColor Gray
Write-Host "  To: $installerPath" -ForegroundColor Gray
Write-Host ""

try {
    Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath -UseBasicParsing
    Write-Host "Download complete!" -ForegroundColor Green
} catch {
    Write-Error "Failed to download installer: $_"
    exit 1
}

# Install
Write-Host ""
Write-Host "Installing Visual C++ Build Tools..." -ForegroundColor Yellow
Write-Host "  Components:" -ForegroundColor Gray
Write-Host "    - MSVC v143 - VS 2022 C++ x64/x86 build tools" -ForegroundColor Gray
Write-Host "    - Windows 11 SDK" -ForegroundColor Gray
Write-Host "    - C++ CMake tools for Windows" -ForegroundColor Gray
Write-Host ""
Write-Host "This will take 10-30 minutes depending on your internet speed." -ForegroundColor Cyan
Write-Host ""

$installArgs = @(
    "--quiet",                    # No UI
    "--wait",                     # Wait for completion
    "--norestart",                # Don't restart automatically
    "--add", "Microsoft.VisualStudio.Workload.VCTools",           # C++ tools
    "--add", "Microsoft.VisualStudio.Component.Windows11SDK.22621", # Windows 11 SDK
    "--add", "Microsoft.VisualStudio.Component.VC.CMake.Project"   # CMake support
)

try {
    if ($WaitForCompletion) {
        # Run synchronously
        $process = Start-Process -FilePath $installerPath -ArgumentList $installArgs -Wait -PassThru
        $exitCode = $process.ExitCode
    } else {
        # Run asynchronously and show spinner
        $process = Start-Process -FilePath $installerPath -ArgumentList $installArgs -PassThru
        
        Write-Host "Installation in progress..." -ForegroundColor Cyan
        $spinner = @('/', '-', '\', '|')
        $i = 0
        
        while (-not $process.HasExited) {
            Write-Host "`r  $($spinner[$i % 4]) Installing..." -NoNewline -ForegroundColor Yellow
            $i++
            Start-Sleep -Milliseconds 500
        }
        
        Write-Host "`r  Installation process completed!    " -ForegroundColor Green
        $exitCode = $process.ExitCode
    }
    
    # Check exit code
    # 0 = Success
    # 3010 = Success, restart required
    if ($exitCode -eq 0 -or $exitCode -eq 3010) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "Installation Successful!" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        Write-Host ""
        
        if ($exitCode -eq 3010) {
            Write-Host "NOTE: A system restart is recommended." -ForegroundColor Yellow
            Write-Host ""
        }
        
        Write-Host "Next steps:" -ForegroundColor White
        Write-Host "  1. Run: .\setup_compiler.ps1" -ForegroundColor Yellow
        Write-Host "  2. Run: .\build.ps1 -Test" -ForegroundColor Yellow
        Write-Host ""
        
        # Verify installation
        if (Test-Path $vswhere) {
            $installPath = & $vswhere -latest -property installationPath 2>$null
            if ($installPath) {
                Write-Host "Installation location: $installPath" -ForegroundColor Gray
            }
        }
    } else {
        Write-Error "Installation failed with exit code: $exitCode"
        Write-Host ""
        Write-Host "Common error codes:" -ForegroundColor Yellow
        Write-Host "  1602 - User cancelled installation" -ForegroundColor Gray
        Write-Host "  1603 - Fatal error during installation" -ForegroundColor Gray
        Write-Host "  3010 - Success, restart required" -ForegroundColor Gray
        exit 1
    }
} catch {
    Write-Error "Installation failed: $_"
    exit 1
} finally {
    # Cleanup
    if (Test-Path $installerPath) {
        Remove-Item $installerPath -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
