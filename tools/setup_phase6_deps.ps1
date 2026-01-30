# DarkAges MMO - Phase 6 Dependencies Setup
# This script installs all external dependencies required for WP-6-1, WP-6-2, WP-6-3, WP-6-4
#
# Requirements:
#   - Windows 10/11
#   - PowerShell 5.1+ or PowerShell Core 7+
#   - Administrator privileges (for some dependencies)
#   - Internet connection

param(
    [switch]$Force,
    [switch]$SkipGNS,
    [switch]$SkipRedis,
    [switch]$SkipScylla,
    [switch]$SkipFlatBuffers,
    [switch]$SkipVcpkg
)

$ErrorActionPreference = "Stop"

# ============================================================================
# Configuration
# ============================================================================

$DepsDir = Join-Path $PSScriptRoot ".." "deps"
$VcpkgDir = Join-Path $DepsDir "vcpkg"
$InstallLog = Join-Path $PSScriptRoot "phase6_deps_install.log"

# Dependency versions
$Versions = @{
    GNS = "v1.4.1"
    Hiredis = "v1.2.0"
    Cassandra = "2.17.1"
    FlatBuffers = "v24.3.25"
}

# ============================================================================
# Logging
# ============================================================================

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logEntry = "[$timestamp] [$Level] $Message"
    Write-Host $logEntry
    Add-Content -Path $InstallLog -Value $logEntry -ErrorAction SilentlyContinue
}

function Test-Admin {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ============================================================================
# Prerequisites
# ============================================================================

function Install-Prerequisites {
    Write-Log "Checking prerequisites..."
    
    # Check Git
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) {
        Write-Log "Git not found. Please install Git for Windows from https://git-scm.com/download/win" "ERROR"
        exit 1
    }
    Write-Log "Git found: $(git --version)"
    
    # Check CMake
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) {
        Write-Log "CMake not found. Installing via winget..." "WARNING"
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            winget install Kitware.CMake
        } else {
            Write-Log "Please install CMake from https://cmake.org/download/" "ERROR"
            exit 1
        }
    }
    Write-Log "CMake found: $(cmake --version | Select-Object -First 1)"
    
    # Check for C++ compiler
    $cl = Get-Command cl -ErrorAction SilentlyContinue
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    
    if (-not ($cl -or $clang -or $gcc)) {
        Write-Log "No C++ compiler found. Please install Visual Studio Build Tools or LLVM/Clang" "ERROR"
        Write-Log "Run ..\install_msvc.ps1 to install MSVC" "INFO"
        exit 1
    }
    
    $compiler = if ($cl) { "MSVC" } elseif ($clang) { "Clang" } else { "GCC" }
    Write-Log "C++ compiler found: $compiler"
    
    # Create deps directory
    if (-not (Test-Path $DepsDir)) {
        New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null
        Write-Log "Created deps directory: $DepsDir"
    }
}

# ============================================================================
# vcpkg Setup
# ============================================================================

function Install-Vcpkg {
    if ($SkipVcpkg) {
        Write-Log "Skipping vcpkg setup"
        return
    }
    
    Write-Log "Setting up vcpkg..."
    
    if (Test-Path $VcpkgDir) {
        if (-not $Force) {
            Write-Log "vcpkg already exists at $VcpkgDir (use -Force to reinstall)"
            return
        }
        Remove-Item -Path $VcpkgDir -Recurse -Force
    }
    
    Push-Location $DepsDir
    try {
        Write-Log "Cloning vcpkg repository..."
        git clone https://github.com/Microsoft/vcpkg.git
        
        Write-Log "Bootstrapping vcpkg..."
        & .\vcpkg\bootstrap-vcpkg.bat
        
        Write-Log "vcpkg setup complete"
    }
    finally {
        Pop-Location
    }
}

# ============================================================================
# GameNetworkingSockets (WP-6-1)
# ============================================================================

function Install-GameNetworkingSockets {
    if ($SkipGNS) {
        Write-Log "Skipping GameNetworkingSockets"
        return
    }
    
    $GNSDir = Join-Path $DepsDir "GameNetworkingSockets"
    
    Write-Log "Installing GameNetworkingSockets $($Versions.GNS)..."
    
    if (Test-Path $GNSDir) {
        if (-not $Force) {
            Write-Log "GameNetworkingSockets already exists (use -Force to reinstall)"
            return
        }
        Remove-Item -Path $GNSDir -Recurse -Force
    }
    
    # Check for OpenSSL
    Write-Log "Checking for OpenSSL..."
    $opensslPath = $env:OPENSSL_ROOT_DIR
    if (-not $opensslPath) {
        # Try common locations
        $commonPaths = @(
            "C:\Program Files\OpenSSL-Win64",
            "C:\Program Files (x86)\OpenSSL-Win64",
            "C:\OpenSSL-Win64"
        )
        foreach ($path in $commonPaths) {
            if (Test-Path $path) {
                $opensslPath = $path
                $env:OPENSSL_ROOT_DIR = $path
                break
            }
        }
    }
    
    if ($opensslPath) {
        Write-Log "OpenSSL found at: $opensslPath"
    } else {
        Write-Log "OpenSSL not found. Installing via winget..." "WARNING"
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            winget install ShiningLight.OpenSSL
            $env:OPENSSL_ROOT_DIR = "C:\Program Files\OpenSSL-Win64"
        } else {
            Write-Log "Please install OpenSSL manually from https://slproweb.com/products/Win32OpenSSL.html" "WARNING"
        }
    }
    
    # Clone and build
    Push-Location $DepsDir
    try {
        Write-Log "Cloning GameNetworkingSockets..."
        git clone --branch $($Versions.GNS) --depth 1 https://github.com/ValveSoftware/GameNetworkingSockets.git
        
        $BuildDir = Join-Path $GNSDir "build"
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
        Push-Location $BuildDir
        
        try {
            Write-Log "Configuring with CMake..."
            $cmakeArgs = @(
                "..",
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_EXAMPLES=OFF",
                "-DBUILD_TESTS=OFF",
                "-DSTEAMWEBRTC=OFF"
            )
            
            if ($env:OPENSSL_ROOT_DIR) {
                $cmakeArgs += "-DOPENSSL_ROOT_DIR=$($env:OPENSSL_ROOT_DIR)"
            }
            
            & cmake @cmakeArgs
            if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
            
            Write-Log "Building..."
            & cmake --build . --config Release --parallel
            if ($LASTEXITCODE -ne 0) { throw "Build failed" }
            
            Write-Log "Installing..."
            & cmake --install . --prefix "$(Join-Path $GNSDir "install")"
            
            Write-Log "GameNetworkingSockets installed successfully"
        }
        finally {
            Pop-Location
        }
    }
    finally {
        Pop-Location
    }
}

# ============================================================================
# hiredis (WP-6-2)
# ============================================================================

function Install-Hiredis {
    if ($SkipRedis) {
        Write-Log "Skipping hiredis"
        return
    }
    
    $HiredisDir = Join-Path $DepsDir "hiredis"
    
    Write-Log "Installing hiredis $($Versions.Hiredis)..."
    
    if (Test-Path $HiredisDir) {
        if (-not $Force) {
            Write-Log "hiredis already exists (use -Force to reinstall)"
            return
        }
        Remove-Item -Path $HiredisDir -Recurse -Force
    }
    
    Push-Location $DepsDir
    try {
        Write-Log "Cloning hiredis..."
        git clone --branch $($Versions.Hiredis) --depth 1 https://github.com/redis/hiredis.git
        
        $BuildDir = Join-Path $HiredisDir "build"
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
        Push-Location $BuildDir
        
        try {
            Write-Log "Configuring with CMake..."
            & cmake .. -G Ninja `
                -DCMAKE_BUILD_TYPE=Release `
                -DENABLE_SSL=OFF `
                -DBUILD_SHARED_LIBS=OFF
            if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
            
            Write-Log "Building..."
            & cmake --build . --config Release --parallel
            if ($LASTEXITCODE -ne 0) { throw "Build failed" }
            
            Write-Log "Installing..."
            & cmake --install . --prefix "$(Join-Path $HiredisDir "install")"
            
            Write-Log "hiredis installed successfully"
        }
        finally {
            Pop-Location
        }
    }
    finally {
        Pop-Location
    }
}

# ============================================================================
# cassandra-cpp-driver (WP-6-3)
# ============================================================================

function Install-CassandraDriver {
    if ($SkipScylla) {
        Write-Log "Skipping cassandra-cpp-driver"
        return
    }
    
    $CassandraDir = Join-Path $DepsDir "cassandra-cpp-driver"
    
    Write-Log "Installing cassandra-cpp-driver $($Versions.Cassandra)..."
    
    if (Test-Path $CassandraDir) {
        if (-not $Force) {
            Write-Log "cassandra-cpp-driver already exists (use -Force to reinstall)"
            return
        }
        Remove-Item -Path $CassandraDir -Recurse -Force
    }
    
    Push-Location $DepsDir
    try {
        Write-Log "Cloning cassandra-cpp-driver..."
        git clone --branch $($Versions.Cassandra) --depth 1 https://github.com/datastax/cpp-driver.git cassandra-cpp-driver
        
        $BuildDir = Join-Path $CassandraDir "build"
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
        Push-Location $BuildDir
        
        try {
            Write-Log "Configuring with CMake..."
            & cmake .. -G Ninja `
                -DCMAKE_BUILD_TYPE=Release `
                -DCASS_BUILD_STATIC=ON `
                -DBUILD_SHARED_LIBS=OFF `
                -DCASS_BUILD_EXAMPLES=OFF `
                -DCASS_BUILD_TESTS=OFF `
                -DCASS_BUILD_DOCS=OFF
            if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
            
            Write-Log "Building..."
            & cmake --build . --config Release --parallel
            if ($LASTEXITCODE -ne 0) { throw "Build failed" }
            
            Write-Log "Installing..."
            & cmake --install . --prefix "$(Join-Path $CassandraDir "install")"
            
            Write-Log "cassandra-cpp-driver installed successfully"
        }
        finally {
            Pop-Location
        }
    }
    finally {
        Pop-Location
    }
}

# ============================================================================
# FlatBuffers (WP-6-4)
# ============================================================================

function Install-FlatBuffers {
    if ($SkipFlatBuffers) {
        Write-Log "Skipping FlatBuffers"
        return
    }
    
    $FBDir = Join-Path $DepsDir "flatbuffers"
    
    Write-Log "Installing FlatBuffers $($Versions.FlatBuffers)..."
    
    if (Test-Path $FBDir) {
        if (-not $Force) {
            Write-Log "FlatBuffers already exists (use -Force to reinstall)"
            return
        }
        Remove-Item -Path $FBDir -Recurse -Force
    }
    
    Push-Location $DepsDir
    try {
        Write-Log "Cloning FlatBuffers..."
        git clone --branch $($Versions.FlatBuffers) --depth 1 https://github.com/google/flatbuffers.git
        
        $BuildDir = Join-Path $FBDir "build"
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
        Push-Location $BuildDir
        
        try {
            Write-Log "Configuring with CMake..."
            & cmake .. -G Ninja `
                -DCMAKE_BUILD_TYPE=Release `
                -DFLATBUFFERS_BUILD_TESTS=OFF `
                -DFLATBUFFERS_BUILD_FLATHASH=OFF `
                -DFLATBUFFERS_BUILD_FLATLIB=OFF `
                -DFLATBUFFERS_BUILD_FLATC=ON
            if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
            
            Write-Log "Building..."
            & cmake --build . --config Release --parallel
            if ($LASTEXITCODE -ne 0) { throw "Build failed" }
            
            Write-Log "Installing..."
            & cmake --install . --prefix "$(Join-Path $FBDir "install")"
            
            # Add flatc to PATH for this session
            $env:PATH = "$(Join-Path $FBDir "install" "bin");$env:PATH"
            
            Write-Log "FlatBuffers installed successfully"
            Write-Log "flatc location: $(Join-Path $FBDir "install" "bin" "flatc.exe")"
        }
        finally {
            Pop-Location
        }
    }
    finally {
        Pop-Location
    }
}

# ============================================================================
# Main
# ============================================================================

function Main {
    Write-Log "========================================"
    Write-Log "DarkAges MMO - Phase 6 Dependencies Setup"
    Write-Log "========================================"
    Write-Log ""
    
    if (Test-Admin) {
        Write-Log "Running with administrator privileges"
    } else {
        Write-Log "Running without administrator privileges (some features may fail)" "WARNING"
    }
    
    # Clear log file
    if (Test-Path $InstallLog) {
        Remove-Item -Path $InstallLog -Force
    }
    
    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    
    try {
        Install-Prerequisites
        Install-Vcpkg
        Install-GameNetworkingSockets
        Install-Hiredis
        Install-CassandraDriver
        Install-FlatBuffers
        
        $Stopwatch.Stop()
        Write-Log ""
        Write-Log "========================================"
        Write-Log "Phase 6 Dependencies Setup Complete!"
        Write-Log "Duration: $($Stopwatch.Elapsed.ToString('hh\:mm\:ss'))"
        Write-Log "========================================"
        Write-Log ""
        Write-Log "Next steps:"
        Write-Log "  1. Start infrastructure: cd infra && docker-compose up -d"
        Write-Log "  2. Build server: .\build.ps1 -Test"
        Write-Log "  3. Begin WP-6-1, WP-6-2, WP-6-3, WP-6-4"
    }
    catch {
        Write-Log "ERROR: $_" "ERROR"
        Write-Log "Stack trace: $($_.ScriptStackTrace)" "ERROR"
        exit 1
    }
}

# Run main function
Main
