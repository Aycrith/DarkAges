#!/usr/bin/env pwsh
<#
.SYNOPSIS
    DarkAges MMO Build Script
.DESCRIPTION
    Automated build script for server and client with CI/CD integration
.PARAMETER Target
    Build target: Server, Client, All
.PARAMETER Configuration
    Build configuration: Debug, Release
.PARAMETER Clean
    Clean before building
.PARAMETER Test
    Run tests after build
.PARAMETER Package
    Create release package
.PARAMETER Version
    Override version (defaults to VERSION file)
.PARAMETER SetupCompiler
    Setup compiler before building
.PARAMETER Compiler
    Compiler to use: Auto, MSVC, Clang, MinGW
#>

param(
    [ValidateSet("Server", "Client", "All")]
    [string]$Target = "All",
    
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    
    [switch]$Clean,
    [switch]$Test,
    [switch]$Package,
    [switch]$Verbose,
    [string]$Version = "",
    [switch]$SetupCompiler,
    [ValidateSet("Auto", "MSVC", "Clang", "MinGW")]
    [string]$Compiler = "Auto"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

# Configuration
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$ArtifactsDir = Join-Path $ProjectRoot "artifacts"
$VersionFile = Join-Path $ProjectRoot "VERSION"

# Read version from file if not provided
if ([string]::IsNullOrEmpty($Version)) {
    if (Test-Path $VersionFile) {
        $Version = (Get-Content $VersionFile -Raw).Trim()
    } else {
        $Version = "0.1.0"
    }
}

# Colors for output
$Green = "`e[32m"
$Red = "`e[31m"
$Yellow = "`e[33m"
$Blue = "`e[34m"
$Cyan = "`e[36m"
$Reset = "`e[0m"

function Write-Status($Message, $Color = $Green) {
    Write-Host "$Color[+] $Message$Reset"
}

function Write-Error($Message) {
    Write-Host "$Red[!] $Message$Reset"
}

function Write-Warning($Message) {
    Write-Host "$Yellow[*] $Message$Reset"
}

function Write-Info($Message) {
    Write-Host "$Blue[>] $Message$Reset"
}

function Write-Section($Message) {
    Write-Host ""
    Write-Host "$Cyan========================================$Reset"
    Write-Host "$Cyan$Message$Reset"
    Write-Host "$Cyan========================================$Reset"
    Write-Host ""
}

# Print banner
Write-Section "DarkAges MMO Build System v$Version"
Write-Info "Target: $Target"
Write-Info "Configuration: $Configuration"
Write-Info "Project Root: $ProjectRoot"
Write-Host ""

# Setup compiler if requested
if ($SetupCompiler -or $Compiler -ne "Auto") {
    $setupScript = Join-Path $ProjectRoot "setup_compiler.ps1"
    if (Test-Path $setupScript) {
        Write-Status "Setting up compiler ($Compiler)..."
        & $setupScript -Compiler $Compiler
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Compiler setup failed"
            exit 1
        }
    } else {
        Write-Warning "setup_compiler.ps1 not found. Using system default compiler."
    }
    Write-Host ""
}

# Step 1: Clean
if ($Clean) {
    Write-Status "Cleaning build directories..."
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Info "Cleaned $BuildDir"
    }
    if (Test-Path $ArtifactsDir) {
        Remove-Item -Recurse -Force $ArtifactsDir
        Write-Info "Cleaned $ArtifactsDir"
    }
    Write-Host ""
}

# Step 2: Create directories
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $ArtifactsDir | Out-Null

# Determine compiler
$hasCompiler = $false
$compilerName = "Unknown"

if (Get-Command cl -ErrorAction SilentlyContinue) {
    $hasCompiler = $true
    $compilerName = "MSVC"
} elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
    $hasCompiler = $true
    $compilerName = "Clang"
} elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    $hasCompiler = $true
    $compilerName = "GCC"
}

Write-Info "Detected compiler: $compilerName"

# ============================================================================
# Build Server
# ============================================================================
if ($Target -eq "Server" -or $Target -eq "All") {
    Write-Section "Building Server ($Configuration)"
    
    # Check prerequisites
    Write-Status "Checking prerequisites..."
    
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) {
        Write-Error "CMake not found. Please install CMake 3.20 or later."
        exit 1
    }
    Write-Info "CMake: $(cmake --version | Select-Object -First 1)"
    
    if (-not $hasCompiler) {
        Write-Error "No C++ compiler detected! Please run: .\setup_compiler.ps1"
        exit 1
    }
    Write-Info "Compiler: $compilerName"
    
    # Check dependencies
    Write-Status "Checking dependencies..."
    $depsOk = $true
    
    if (-not (Test-Path "$ProjectRoot/deps/entt/single_include/entt/entt.hpp")) {
        Write-Warning "EnTT not found. Fetching..."
        git clone --depth 1 --branch v3.13.0 https://github.com/skypjack/entt.git "$ProjectRoot/deps/entt" 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { Write-Info "EnTT: Fetched" }
        else { $depsOk = $false }
    } else {
        Write-Info "EnTT: OK"
    }
    
    if (-not (Test-Path "$ProjectRoot/deps/glm/glm/glm.hpp")) {
        Write-Warning "GLM not found. Fetching..."
        git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git "$ProjectRoot/deps/glm" 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { Write-Info "GLM: Fetched" }
        else { $depsOk = $false }
    } else {
        Write-Info "GLM: OK"
    }
    
    if (-not $depsOk) {
        Write-Error "Failed to fetch dependencies"
        exit 1
    }
    
    # Configure CMake
    Write-Status "Configuring CMake..."
    Set-Location $BuildDir
    
    $cmakeArgs = @(
        ".."
        "-DCMAKE_BUILD_TYPE=$Configuration"
        "-DBUILD_TESTS=ON"
        "-DBUILD_SHARED_LIBS=OFF"
    )
    
    # Add compiler-specific options
    if ($compilerName -eq "MSVC") {
        $ninja = Get-Command ninja -ErrorAction SilentlyContinue
        if ($ninja) {
            $cmakeArgs += "-GNinja"
            Write-Info "Using Ninja generator"
        }
    } elseif ($compilerName -eq "MinGW") {
        $cmakeArgs += "-GMinGW Makefiles"
    }
    
    if ($Verbose) {
        $cmakeArgs += "-DCMAKE_VERBOSE_MAKEFILE=ON"
    }
    
    & cmake @cmakeArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed"
        Set-Location $ProjectRoot
        exit 1
    }
    Write-Info "CMake configuration successful"
    
    # Build
    Write-Status "Building server..."
    $buildArgs = @("--build", ".", "--config", $Configuration)
    
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
        Write-Error "Build failed"
        Set-Location $ProjectRoot
        exit 1
    }
    Write-Status "Server build complete"
    
    Set-Location $ProjectRoot
}

# ============================================================================
# Run Tests
# ============================================================================
if ($Test -and ($Target -eq "Server" -or $Target -eq "All")) {
    Write-Section "Running Tests"
    
    Set-Location $BuildDir
    & ctest -C $Configuration --output-on-failure 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Tests failed"
        Set-Location $ProjectRoot
        exit 1
    }
    
    Write-Status "All tests passed"
    Set-Location $ProjectRoot
}

# ============================================================================
# Build Client
# ============================================================================
if ($Target -eq "Client" -or $Target -eq "All") {
    Write-Section "Building Client"
    
    $ClientDir = Join-Path $ProjectRoot "src" "client"
    
    if (-not (Test-Path $ClientDir)) {
        Write-Warning "Client directory not found at $ClientDir - skipping client build"
    } else {
        # Check for Godot
        $GodotExe = $null
        $godotVariants = @("godot", "godot4", "Godot_v4", "godot.exe", "Godot.exe")
        
        foreach ($variant in $godotVariants) {
            $cmd = Get-Command $variant -ErrorAction SilentlyContinue
            if ($cmd) {
                $GodotExe = $variant
                break
            }
        }
        
        if (-not $GodotExe) {
            Write-Warning "Godot not found in PATH. Skipping client build."
            Write-Info "Please install Godot 4.2+ and add it to PATH"
        } else {
            Write-Info "Using Godot: $GodotExe"
            
            Set-Location $ClientDir
            
            # Build C# solutions
            Write-Status "Building C# solutions..."
            & $GodotExe --build-solutions --headless 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Client C# build failed"
                Set-Location $ProjectRoot
                exit 1
            }
            
            # Export if packaging
            if ($Package) {
                Write-Status "Exporting client..."
                $ExportDir = Join-Path $ArtifactsDir "client"
                New-Item -ItemType Directory -Force -Path $ExportDir | Out-Null
                
                # Determine platform
                $platform = "Windows Desktop"
                $ext = ".exe"
                if ($IsLinux) {
                    $platform = "Linux/X11"
                    $ext = ".x86_64"
                } elseif ($IsMacOS) {
                    $platform = "macOS"
                    $ext = ".app"
                }
                
                $exportPath = Join-Path $ExportDir "DarkAgesClient$ext"
                & $GodotExe --export-release "$platform" $exportPath --headless 2>&1
                if ($LASTEXITCODE -ne 0) {
                    Write-Warning "Client export may have issues, but build succeeded"
                } else {
                    Write-Info "Exported to: $exportPath"
                }
            }
            
            Write-Status "Client build complete"
            Set-Location $ProjectRoot
        }
    }
}

# ============================================================================
# Package Release
# ============================================================================
if ($Package) {
    Write-Section "Creating Release Package"
    
    $PackageDir = Join-Path $ArtifactsDir "DarkAges-v$Version"
    New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
    
    # Server binaries
    $ServerOut = Join-Path $PackageDir "server"
    New-Item -ItemType Directory -Force -Path $ServerOut | Out-Null
    
    $serverExePaths = @(
        (Join-Path $BuildDir $Configuration "darkages_server.exe"),
        (Join-Path $BuildDir "darkages_server.exe"),
        (Join-Path $BuildDir "darkages_server"),
        (Join-Path $BuildDir "Release" "darkages_server.exe"),
        (Join-Path $BuildDir "Release" "darkages_server")
    )
    
    $serverFound = $false
    foreach ($exePath in $serverExePaths) {
        if (Test-Path $exePath) {
            Copy-Item $exePath $ServerOut
            Write-Info "Copied server binary: $(Split-Path $exePath -Leaf)"
            $serverFound = $true
            break
        }
    }
    
    if (-not $serverFound) {
        Write-Warning "Server binary not found in build directory"
    }
    
    # Copy DLLs if on Windows
    if ($IsWindows -or ($PSVersionTable.PSVersion.Major -lt 6)) {
        $dllFiles = Get-ChildItem -Path $BuildDir -Filter "*.dll" -ErrorAction SilentlyContinue
        if ($dllFiles) {
            Copy-Item $dllFiles.FullName $ServerOut -ErrorAction SilentlyContinue
            Write-Info "Copied $($dllFiles.Count) DLL files"
        }
    }
    
    # Configuration files
    $infraDir = Join-Path $ProjectRoot "infra"
    if (Test-Path $infraDir) {
        $composeFiles = Get-ChildItem -Path $infraDir -Filter "docker-compose*.yml" -ErrorAction SilentlyContinue
        foreach ($file in $composeFiles) {
            Copy-Item $file.FullName $ServerOut
            Write-Info "Copied: $($file.Name)"
        }
    }
    
    # Scripts
    $scripts = @("quickstart.ps1", "verify_build.bat", "verify_build.sh")
    foreach ($script in $scripts) {
        $scriptPath = Join-Path $ProjectRoot $script
        if (Test-Path $scriptPath) {
            Copy-Item $scriptPath $ServerOut
            Write-Info "Copied: $script"
        }
    }
    
    # Client
    $ClientArtifacts = Join-Path $ArtifactsDir "client"
    if (Test-Path $ClientArtifacts) {
        $ClientOut = Join-Path $PackageDir "client"
        Copy-Item -Recurse $ClientArtifacts $ClientOut
        Write-Info "Copied client artifacts"
    }
    
    # Documentation
    $docFiles = @("README.md", "LICENSE", "CHANGELOG.md", "CONTRIBUTING.md")
    foreach ($doc in $docFiles) {
        $docPath = Join-Path $ProjectRoot $doc
        if (Test-Path $docPath) {
            Copy-Item $docPath $PackageDir
            Write-Info "Copied: $doc"
        }
    }
    
    # Version info
    @"
DarkAges MMO v$Version
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Configuration: $Configuration
Compiler: $compilerName
"@ | Out-File -FilePath (Join-Path $PackageDir "VERSION.txt") -Encoding UTF8
    
    # Create archive
    $osSuffix = "win64"
    if ($IsLinux) { $osSuffix = "linux" }
    elseif ($IsMacOS) { $osSuffix = "macos" }
    
    $ZipFile = Join-Path $ArtifactsDir "DarkAges-v$Version-$osSuffix.zip"
    
    Write-Status "Creating archive: $(Split-Path $ZipFile -Leaf)"
    Compress-Archive -Path $PackageDir -DestinationPath $ZipFile -Force
    
    Write-Status "Package created: $ZipFile"
    
    # Calculate checksum
    $hash = Get-FileHash $ZipFile -Algorithm SHA256
    $hash.Hash | Out-File -FilePath "$ZipFile.sha256" -Encoding UTF8
    Write-Info "SHA256: $($hash.Hash)"
}

# ============================================================================
# Summary
# ============================================================================
Write-Section "Build Summary"

Write-Host "Version: $Version" -ForegroundColor White
Write-Host "Target: $Target" -ForegroundColor White
Write-Host "Configuration: $Configuration" -ForegroundColor White
Write-Host "Compiler: $compilerName" -ForegroundColor White
Write-Host ""

if ($Target -eq "Server" -or $Target -eq "All") {
    Write-Host "Server:" -ForegroundColor Cyan
    $serverExeFound = $false
    $exePaths = @(
        (Join-Path $BuildDir $Configuration "darkages_server.exe"),
        (Join-Path $BuildDir "darkages_server.exe"),
        (Join-Path $BuildDir "darkages_server")
    )
    foreach ($path in $exePaths) {
        if (Test-Path $path) {
            Write-Host "  ✓ $path" -ForegroundColor Green
            $serverExeFound = $true
            break
        }
    }
    if (-not $serverExeFound) {
        Write-Host "  ✗ Not found" -ForegroundColor Red
    }
}

if ($Test) {
    Write-Host "Tests: $(if ($LASTEXITCODE -eq 0) { '✓ PASSED' } else { '✗ FAILED' })" -ForegroundColor $(if ($LASTEXITCODE -eq 0) { 'Green' } else { 'Red' })
}

if ($Package) {
    Write-Host ""
    Write-Host "Artifacts:" -ForegroundColor Cyan
    if (Test-Path $ArtifactsDir) {
        Get-ChildItem $ArtifactsDir -Recurse -File | ForEach-Object {
            $size = if ($_.Length -gt 1MB) { "{0:N2} MB" -f ($_.Length / 1MB) } else { "{0:N2} KB" -f ($_.Length / 1KB) }
            Write-Host "  - $($_.Name) ($size)" -ForegroundColor Gray
        }
    }
}

Write-Host ""
Write-Status "Build completed successfully!"
Write-Host ""
