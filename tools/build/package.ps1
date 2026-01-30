#!/usr/bin/env pwsh
<#
.SYNOPSIS
    DarkAges MMO Local Packaging Script (PowerShell)
.DESCRIPTION
    Creates a release package from existing build artifacts
.PARAMETER Version
    Package version (defaults to VERSION file)
.PARAMETER SkipClient
    Skip client packaging
.EXAMPLE
    .\tools\build\package.ps1
    .\tools\build\package.ps1 -Version 0.8.0
#>

param(
    [string]$Version = "",
    [switch]$SkipClient
)

$ErrorActionPreference = "Stop"

# Colors
$Green = "`e[32m"
$Red = "`e[31m"
$Yellow = "`e[33m"
$Blue = "`e[34m"
$Cyan = "`e[36m"
$Reset = "`e[0m"

function Write-Status($Message) {
    Write-Host "$Green[+] $Message$Reset"
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

# Configuration
$ScriptDir = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $ProjectRoot "build"
$ArtifactsDir = Join-Path $ProjectRoot "artifacts"
$VersionFile = Join-Path $ProjectRoot "VERSION"

# Get version
if ([string]::IsNullOrEmpty($Version)) {
    if (Test-Path $VersionFile) {
        $Version = (Get-Content $VersionFile -Raw).Trim()
    } else {
        $Version = "0.1.0"
    }
}

Write-Section "DarkAges MMO Packaging Script"
Write-Info "Version: $Version"
Write-Info "Project Root: $ProjectRoot"

# Check for build artifacts
if (-not (Test-Path $BuildDir)) {
    Write-Error "Build directory not found: $BuildDir"
    Write-Info "Run .\build.ps1 first"
    exit 1
}

# Detect OS
$OS = "win64"
if ($IsLinux) { $OS = "linux" }
elseif ($IsMacOS) { $OS = "macos" }

# Create directories
New-Item -ItemType Directory -Force -Path $ArtifactsDir | Out-Null

$PackageDir = Join-Path $ArtifactsDir "DarkAges-v$Version"
if (Test-Path $PackageDir) {
    Write-Warning "Removing existing package directory"
    Remove-Item -Recurse -Force $PackageDir
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

# ============================================================================
# Package Server
# ============================================================================
Write-Section "Packaging Server"

$ServerOut = Join-Path $PackageDir "server"
New-Item -ItemType Directory -Force -Path $ServerOut | Out-Null

# Find server executable
$ServerFound = $false
$exePaths = @(
    (Join-Path $BuildDir "Release" "darkages_server.exe"),
    (Join-Path $BuildDir "darkages_server.exe"),
    (Join-Path $BuildDir "Release" "darkages_server"),
    (Join-Path $BuildDir "darkages_server")
)

foreach ($path in $exePaths) {
    if (Test-Path $path) {
        Copy-Item $path $ServerOut
        Write-Info "Copied: $(Split-Path $path -Leaf)"
        $ServerFound = $true
        break
    }
}

if (-not $ServerFound) {
    Write-Error "Server executable not found in $BuildDir"
    exit 1
}

# Copy DLLs
$dllFiles = Get-ChildItem -Path $BuildDir -Filter "*.dll" -ErrorAction SilentlyContinue
if ($dllFiles) {
    Copy-Item $dllFiles.FullName $ServerOut -ErrorAction SilentlyContinue
    Write-Info "Copied $($dllFiles.Count) DLL files"
}

# Copy configuration files
$infraDir = Join-Path $ProjectRoot "infra"
if (Test-Path $infraDir) {
    $composeFiles = Get-ChildItem -Path $infraDir -Filter "docker-compose*.yml" -ErrorAction SilentlyContinue
    foreach ($file in $composeFiles) {
        Copy-Item $file.FullName $ServerOut
        Write-Info "Copied: $($file.Name)"
    }
}

# Copy scripts
$scripts = @("quickstart.ps1", "verify_build.bat", "verify_build.sh")
foreach ($script in $scripts) {
    $scriptPath = Join-Path $ProjectRoot $script
    if (Test-Path $scriptPath) {
        Copy-Item $scriptPath $ServerOut
        Write-Info "Copied: $script"
    }
}

# ============================================================================
# Package Client
# ============================================================================
Write-Section "Packaging Client"

$ClientArtifacts = Join-Path $ArtifactsDir "client"
$ClientDir = Join-Path $ProjectRoot "src" "client"

if ((-not $SkipClient) -and (Test-Path $ClientArtifacts)) {
    $ClientOut = Join-Path $PackageDir "client"
    Copy-Item -Recurse $ClientArtifacts $ClientOut
    Write-Info "Copied client artifacts"
} elseif (Test-Path $ClientDir) {
    Write-Warning "Client artifacts not found. Run .\build.ps1 -Package to build client."
} else {
    Write-Warning "Client directory not found"
}

# ============================================================================
# Package Documentation
# ============================================================================
Write-Section "Packaging Documentation"

$docs = @("README.md", "LICENSE", "CHANGELOG.md", "CONTRIBUTING.md", "COMPILER_SETUP.md")
foreach ($doc in $docs) {
    $docPath = Join-Path $ProjectRoot $doc
    if (Test-Path $docPath) {
        Copy-Item $docPath $PackageDir
        Write-Info "Copied: $doc"
    }
}

# Create version info
Write-Info "Creating VERSION.txt"
@"
DarkAges MMO v$Version
Package Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
OS: $OS
"@ | Out-File -FilePath (Join-Path $PackageDir "VERSION.txt") -Encoding UTF8

# Create README for package
$quickstart = @"
# DarkAges MMO - Quick Start Guide

## Server Setup

### Option 1: Direct Run (Windows)
```powershell
cd server
.\darkages_server.exe
```

### Option 2: Docker Compose
```powershell
cd server
docker-compose up -d
```

## Client Setup

Run `client\DarkAgesClient.exe`

## Configuration

Server configuration files are located in:
- `server\config\` (if exists)
- Environment variables (see README.md)

## Support

For detailed documentation, see README.md
For build instructions, see COMPILER_SETUP.md
"@

$quickstart | Out-File -FilePath (Join-Path $PackageDir "QUICKSTART.md") -Encoding UTF8
Write-Info "Created QUICKSTART.md"

# ============================================================================
# Create Archive
# ============================================================================
Write-Section "Creating Archive"

$ArchiveName = "DarkAges-v$Version-$OS.zip"
$ArchivePath = Join-Path $ArtifactsDir $ArchiveName

Write-Info "Creating: $ArchiveName"
Compress-Archive -Path $PackageDir -DestinationPath $ArchivePath -Force

if (Test-Path $ArchivePath) {
    Write-Status "Archive created: $ArchiveName"
    
    # Calculate checksum
    Write-Info "Calculating checksum..."
    $hash = Get-FileHash $ArchivePath -Algorithm SHA256
    $hash.Hash | Out-File -FilePath "$ArchivePath.sha256" -Encoding UTF8
    Write-Info "SHA256: $($hash.Hash)"
    Write-Info "Checksum file: $ArchiveName.sha256"
    
    # Display file size
    $size = (Get-Item $ArchivePath).Length
    if ($size -gt 1MB) {
        Write-Info "Size: $([math]::Round($size / 1MB, 2)) MB"
    } else {
        Write-Info "Size: $([math]::Round($size / 1KB, 2)) KB"
    }
} else {
    Write-Warning "Archive creation may have failed"
}

# ============================================================================
# Summary
# ============================================================================
Write-Section "Packaging Summary"

Write-Host "Version: $Version"
Write-Host "OS: $OS"
Write-Host "Package Directory: $PackageDir"
Write-Host "Archive: $ArchivePath"
Write-Host ""

Write-Status "Packaging completed successfully!"
Write-Host ""

# List package contents
Write-Host "Package contents:"
Get-ChildItem $PackageDir -Recurse -File | Select-Object -First 20 | ForEach-Object {
    $relPath = $_.FullName.Substring($PackageDir.Length + 1)
    Write-Host "  $relPath"
}

$totalFiles = (Get-ChildItem $PackageDir -Recurse -File).Count
if ($totalFiles -gt 20) {
    Write-Host "  ... and $($totalFiles - 20) more files"
}

Write-Host ""
