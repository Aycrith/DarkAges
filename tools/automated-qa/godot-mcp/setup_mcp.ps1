# Setup Godot MCP Server for DarkAges MMO Testing
# This script installs and configures the Godot MCP server

param(
    [string]$MCPRepo = "https://github.com/bradypp/godot-mcp.git",
    [string]$InstallDir = "$PSScriptRoot/../../../tools/godot-mcp",
    [string]$GodotPath = $env:GODOT_PATH,
    [switch]$Force
)

Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host "Godot MCP Server Setup for DarkAges MMO" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host ""

# Check prerequisites
Write-Host "[1/5] Checking prerequisites..." -ForegroundColor Yellow

# Check Node.js
$nodeVersion = & node --version 2>$null
if (-not $nodeVersion) {
    Write-Host "ERROR: Node.js not found. Please install Node.js 18+ from https://nodejs.org/" -ForegroundColor Red
    exit 1
}
Write-Host "  Node.js: $nodeVersion" -ForegroundColor Green

# Check npm
$npmVersion = & npm --version 2>$null
if (-not $npmVersion) {
    Write-Host "ERROR: npm not found" -ForegroundColor Red
    exit 1
}
Write-Host "  npm: $npmVersion" -ForegroundColor Green

# Check Git
$gitVersion = & git --version 2>$null
if (-not $gitVersion) {
    Write-Host "ERROR: Git not found" -ForegroundColor Red
    exit 1
}
Write-Host "  Git: $gitVersion" -ForegroundColor Green

# Check Godot
if (-not $GodotPath) {
    # Try to find Godot in common locations
    $possiblePaths = @(
        "C:\Tools\Godot\Godot_v4.2.exe",
        "C:\Tools\Godot\Godot_v4.3.exe",
        "C:\Program Files\Godot\Godot.exe",
        "C:\Program Files\Godot\Godot_v4.2.exe"
    )
    
    foreach ($path in $possiblePaths) {
        if (Test-Path $path) {
            $GodotPath = $path
            break
        }
    }
}

if (-not $GodotPath -or -not (Test-Path $GodotPath)) {
    Write-Host "WARNING: Godot not found at: $GodotPath" -ForegroundColor Yellow
    Write-Host "  Set GODOT_PATH environment variable or provide --GodotPath" -ForegroundColor Yellow
    $GodotPath = Read-Host "Enter path to Godot executable (or press Enter to skip)"
}

if ($GodotPath -and (Test-Path $GodotPath)) {
    Write-Host "  Godot: $GodotPath" -ForegroundColor Green
} else {
    Write-Host "  Godot: Not found (will need manual configuration)" -ForegroundColor Yellow
}

Write-Host ""

# Clone or update repository
Write-Host "[2/5] Installing Godot MCP server..." -ForegroundColor Yellow

if (Test-Path $InstallDir) {
    if ($Force) {
        Write-Host "  Removing existing installation..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $InstallDir
    } else {
        Write-Host "  Found existing installation at: $InstallDir" -ForegroundColor Green
        $response = Read-Host "  Update existing installation? (y/n)"
        if ($response -eq "y") {
            Write-Host "  Updating..." -ForegroundColor Yellow
            Set-Location $InstallDir
            & git pull
            Set-Location -
        }
    }
}

if (-not (Test-Path $InstallDir)) {
    Write-Host "  Cloning from $MCPRepo..." -ForegroundColor Yellow
    & git clone $MCPRepo $InstallDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to clone repository" -ForegroundColor Red
        exit 1
    }
}

Write-Host "  Installed to: $InstallDir" -ForegroundColor Green
Write-Host ""

# Install dependencies
Write-Host "[3/5] Installing dependencies..." -ForegroundColor Yellow
Set-Location $InstallDir

Write-Host "  Running npm install..." -ForegroundColor Yellow
& npm install
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: npm install failed" -ForegroundColor Red
    exit 1
}

Write-Host "  Building server..." -ForegroundColor Yellow
& npm run build
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Set-Location -
Write-Host "  Build successful" -ForegroundColor Green
Write-Host ""

# Create configuration
Write-Host "[4/5] Creating configuration..." -ForegroundColor Yellow

$projectRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
$clientPath = "$projectRoot\src\client"

$config = @{
    mcpServers = @{
        godot = @{
            command = "node"
            args = @("$InstallDir\build\index.js")
            env = @{
                GODOT_PATH = $GodotPath
                DEBUG = "true"
                READ_ONLY_MODE = "false"
            }
            disabled = $false
            autoApprove = @(
                "launch_editor"
                "run_project"
                "get_debug_output"
                "stop_project"
                "take_screenshot"
                "create_scene"
                "add_node"
            )
        }
    }
    darkages = @{
        client_project_path = $clientPath
        server_address = "localhost:7777"
        test_scenarios_path = "$projectRoot\tools\automated-qa\scenarios"
        screenshot_output_path = "$projectRoot\tools\automated-qa\screenshots"
    }
}

$configPath = "$PSScriptRoot\config.json"
$config | ConvertTo-Json -Depth 10 | Set-Content $configPath

Write-Host "  Configuration saved to: $configPath" -ForegroundColor Green
Write-Host ""

# Create Cline settings example
Write-Host "[5/5] Creating Cline configuration example..." -ForegroundColor Yellow

$clineConfig = @{
    mcpServers = @{
        godot = @{
            command = "node"
            args = @("$InstallDir\build\index.js")
            env = @{
                GODOT_PATH = $GodotPath
                DEBUG = "true"
            }
        }
    }
}

$clinePath = "$PSScriptRoot\cline_mcp_settings.example.json"
$clineConfig | ConvertTo-Json -Depth 10 | Set-Content $clinePath

Write-Host "  Cline config example: $clinePath" -ForegroundColor Green
Write-Host ""

# Summary
Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host "Setup Complete!" -ForegroundColor Green
Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host ""
Write-Host "1. Add MCP server to your AI assistant configuration:" -ForegroundColor White
Write-Host ""
Write-Host "   For Cline (VS Code):" -ForegroundColor Cyan
Write-Host "   Copy the example configuration from:" -ForegroundColor Gray
Write-Host "   $clinePath" -ForegroundColor Gray
Write-Host "   to:" -ForegroundColor Gray
Write-Host "   %APPDATA%\Code\User\globalStorage\saoudrizwan.claude-dev\settings\cline_mcp_settings.json" -ForegroundColor Gray
Write-Host ""
Write-Host "   For Cursor:" -ForegroundColor Cyan
Write-Host "   Add to .cursor/mcp.json in your project root:" -ForegroundColor Gray
Write-Host "   See: $clinePath" -ForegroundColor Gray
Write-Host ""
Write-Host "2. Test the installation:" -ForegroundColor White
Write-Host "   python tools/automated-qa/godot-mcp/test_mcp_connection.py" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Run integration tests:" -ForegroundColor White
Write-Host "   python tools/automated-qa/godot-mcp/test_movement_sync_mcp.py --server build/Release/darkages_server.exe" -ForegroundColor Gray
Write-Host ""
Write-Host "==============================================================" -ForegroundColor Cyan
