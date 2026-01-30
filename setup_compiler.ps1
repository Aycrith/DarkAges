#!/usr/bin/env powershell
# DarkAges MMO - Compiler Setup Script
# Automatically detects and configures the best available C++ compiler

param(
    [ValidateSet("Auto", "MSVC", "Clang", "MinGW")]
    [string]$Compiler = "Auto"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DarkAges Compiler Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

function Find-MSVC {
    Write-Host "Looking for MSVC..." -ForegroundColor Yellow
    
    # Try vswhere
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($installPath) {
            Write-Host "  Found MSVC at: $installPath" -ForegroundColor Green
            return $installPath
        }
    }
    
    # Try common paths
    $commonPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path "$path\VC\Auxiliary\Build\vcvars64.bat") {
            Write-Host "  Found MSVC at: $path" -ForegroundColor Green
            return $path
        }
    }
    
    return $null
}

function Find-Clang {
    Write-Host "Looking for Clang..." -ForegroundColor Yellow
    
    $clang = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($clang) {
        Write-Host "  Found Clang at: $($clang.Source)" -ForegroundColor Green
        return $clang.Source
    }
    
    # Try common paths
    $commonPaths = @(
        "C:\Program Files\LLVM\bin\clang++.exe",
        "C:\Program Files (x86)\LLVM\bin\clang++.exe"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            Write-Host "  Found Clang at: $path" -ForegroundColor Green
            return $path
        }
    }
    
    return $null
}

function Find-MinGW {
    Write-Host "Looking for MinGW/GCC..." -ForegroundColor Yellow
    
    $gcc = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gcc) {
        Write-Host "  Found GCC at: $($gcc.Source)" -ForegroundColor Green
        return $gcc.Source
    }
    
    # Try common paths
    $commonPaths = @(
        "C:\msys64\ucrt64\bin\g++.exe",
        "C:\mingw64\bin\g++.exe",
        "C:\mingw\bin\g++.exe"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            Write-Host "  Found GCC at: $path" -ForegroundColor Green
            return $path
        }
    }
    
    return $null
}

function Setup-MSVC {
    param([string]$InstallPath)
    
    Write-Host "`nConfiguring MSVC environment..." -ForegroundColor Cyan
    
    # Try to use vsdevcmd
    $devCmd = "$InstallPath\Common7\Tools\VsDevCmd.bat"
    $vcvars = "$InstallPath\VC\Auxiliary\Build\vcvars64.bat"
    
    if (Test-Path $devCmd) {
        # Import the environment from vsdevcmd
        $output = & cmd /c "`"$devCmd`" -arch=amd64 -host_arch=amd64 && set" 2>$null
    } elseif (Test-Path $vcvars) {
        # Import the environment from vcvars
        $output = & cmd /c "`"$vcvars`" && set" 2>$null
    } else {
        Write-Error "Could not find MSVC environment script"
    }
    
    # Parse and set environment variables
    $output | ForEach-Object {
        if ($_ -match '^(\w+)=(.*)$') {
            $name = $matches[1]
            $value = $matches[2]
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }
    
    Write-Host "MSVC environment configured!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Compiler version:" -ForegroundColor Yellow
    cl.exe 2>&1 | Select-Object -First 1
}

function Setup-Clang {
    param([string]$ClangPath)
    
    Write-Host "`nConfiguring Clang environment..." -ForegroundColor Cyan
    
    $env:CXX = "clang++"
    $env:CC = "clang"
    
    Write-Host "Clang environment configured!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Compiler version:" -ForegroundColor Yellow
    clang++ --version | Select-Object -First 1
}

function Setup-MinGW {
    param([string]$GccPath)
    
    Write-Host "`nConfiguring MinGW environment..." -ForegroundColor Cyan
    
    $env:CXX = "g++"
    $env:CC = "gcc"
    
    # Add to path if not already there
    $binDir = Split-Path $GccPath -Parent
    if ($env:Path -notlike "*$binDir*") {
        $env:Path = "$binDir;$env:Path"
    }
    
    Write-Host "MinGW environment configured!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Compiler version:" -ForegroundColor Yellow
    g++ --version | Select-Object -First 1
}

# Main logic
$selectedCompiler = $Compiler

if ($Compiler -eq "Auto") {
    Write-Host "Auto-detecting best available compiler..." -ForegroundColor Yellow
    Write-Host ""
    
    # Priority: MSVC > Clang > MinGW
    $msvcPath = Find-MSVC
    if ($msvcPath) {
        $selectedCompiler = "MSVC"
    } else {
        $clangPath = Find-Clang
        if ($clangPath) {
            $selectedCompiler = "Clang"
        } else {
            $mingwPath = Find-MinGW
            if ($mingwPath) {
                $selectedCompiler = "MinGW"
            } else {
                Write-Error @"
No C++ compiler found!

Please install one of the following:
1. Visual Studio 2022 Build Tools (Recommended)
   Download: https://aka.ms/vs/17/release/vs_buildtools.exe
   Install: vs_buildtools.exe --add Microsoft.VisualStudio.Workload.VCTools

2. LLVM/Clang
   Run: winget install LLVM.LLVM

3. MinGW-w64
   Download: https://www.msys2.org/

See COMPILER_SETUP.md for detailed instructions.
"@
            }
        }
    }
}

Write-Host "Selected compiler: $selectedCompiler" -ForegroundColor Green
Write-Host ""

# Configure the selected compiler
switch ($selectedCompiler) {
    "MSVC" {
        $path = if ($msvcPath) { $msvcPath } else { Find-MSVC }
        if (-not $path) {
            Write-Error "MSVC not found. Install from https://visualstudio.microsoft.com/downloads/"
        }
        Setup-MSVC -InstallPath $path
    }
    
    "Clang" {
        $path = if ($clangPath) { $clangPath } else { Find-Clang }
        if (-not $path) {
            Write-Error "Clang not found. Install with: winget install LLVM.LLVM"
        }
        Setup-Clang -ClangPath $path
    }
    
    "MinGW" {
        $path = if ($mingwPath) { $mingwPath } else { Find-MinGW }
        if (-not $path) {
            Write-Error "MinGW not found. Install MSYS2 from https://www.msys2.org/"
        }
        Setup-MinGW -GccPath $path
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Setup Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "You can now build the project with:" -ForegroundColor White
Write-Host "  .\build.ps1 -Test" -ForegroundColor Yellow
Write-Host ""
Write-Host "Or manually:" -ForegroundColor White
Write-Host "  mkdir build; cd build" -ForegroundColor Gray
Write-Host "  cmake .. -DBUILD_TESTS=ON" -ForegroundColor Gray
Write-Host "  cmake --build . --parallel" -ForegroundColor Gray
Write-Host ""
