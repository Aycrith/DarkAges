#!/usr/bin/env pwsh
# DarkAges Infrastructure Setup Script
# This script starts Redis and ScyllaDB containers

param(
    [switch]$Stop,
    [switch]$Restart,
    [switch]$Logs
)

$DockerBin = "C:\Program Files\Docker\Docker\resources\bin"
$docker = Join-Path $DockerBin "docker.exe"
$composeFile = "C:\Dev\DarkAges\infra\docker-compose.dev.yml"

function Test-DockerReady {
    try {
        $null = & $docker info 2>&1
        return $LASTEXITCODE -eq 0
    } catch {
        return $false
    }
}

function Start-Infrastructure {
    Write-Host "Starting DarkAges infrastructure..." -ForegroundColor Cyan
    
    if (!(Test-DockerReady)) {
        Write-Host "❌ Docker is not running. Please start Docker Desktop first." -ForegroundColor Red
        Write-Host "   Docker Desktop location: C:\Program Files\Docker\Docker\Docker Desktop.exe"
        exit 1
    }
    
    Write-Host "Stopping any existing containers..."
    & $docker compose -f $composeFile down 2>&1 | Out-Null
    
    Write-Host "Starting Redis and ScyllaDB containers..."
    & $docker compose -f $composeFile up -d
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Failed to start containers" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "Waiting for services to initialize..." -ForegroundColor Yellow
    
    # Wait for Redis
    Write-Host "Checking Redis..." -NoNewline
    $redisReady = $false
    $attempts = 0
    while (!$redisReady -and $attempts -lt 30) {
        Start-Sleep -Seconds 1
        $attempts++
        try {
            $result = & $docker exec darkages-redis redis-cli ping 2>&1
            if ($result -eq "PONG") {
                $redisReady = $true
                Write-Host " ✅ Ready" -ForegroundColor Green
            }
        } catch {}
    }
    if (!$redisReady) {
        Write-Host " ⚠️ Timeout waiting for Redis" -ForegroundColor Yellow
    }
    
    # Wait for ScyllaDB
    Write-Host "Checking ScyllaDB (this may take 60+ seconds)..." -NoNewline
    $scyllaReady = $false
    $attempts = 0
    while (!$scyllaReady -and $attempts -lt 60) {
        Start-Sleep -Seconds 2
        $attempts++
        Write-Host "." -NoNewline
        try {
            $result = & $docker exec darkages-scylla cqlsh -e "SELECT now() FROM system.local;" 2>&1
            if ($LASTEXITCODE -eq 0) {
                $scyllaReady = $true
            }
        } catch {}
    }
    if ($scyllaReady) {
        Write-Host " ✅ Ready" -ForegroundColor Green
    } else {
        Write-Host " ⚠️ Timeout waiting for ScyllaDB" -ForegroundColor Yellow
    }
    
    Write-Host ""
    Write-Host "==============================================" -ForegroundColor Cyan
    Write-Host "Infrastructure Status:" -ForegroundColor Cyan
    Write-Host "----------------------------------------------"
    if ($redisReady) {
        Write-Host "Redis    : ✅ Running on port 6379" -ForegroundColor Green
    } else {
        Write-Host "Redis    : ⚠️ Status unknown" -ForegroundColor Yellow
    }
    if ($scyllaReady) {
        Write-Host "ScyllaDB : ✅ Running on port 9042" -ForegroundColor Green
    } else {
        Write-Host "ScyllaDB : ⏳ Still initializing..." -ForegroundColor Yellow
    }
    Write-Host "----------------------------------------------"
    Write-Host ""
    Write-Host "View logs: .\tools\validation\setup_infrastructure.ps1 -Logs"
    Write-Host "Check status: .\tools\validation\check_services.ps1"
}

function Stop-Infrastructure {
    Write-Host "Stopping DarkAges infrastructure..." -ForegroundColor Cyan
    & $docker compose -f $composeFile down
    Write-Host "✅ Services stopped" -ForegroundColor Green
}

function Show-Logs {
    & $docker compose -f $composeFile logs -f
}

# Main execution
if ($Logs) {
    Show-Logs
} elseif ($Stop) {
    Stop-Infrastructure
} elseif ($Restart) {
    Stop-Infrastructure
    Start-Sleep -Seconds 2
    Start-Infrastructure
} else {
    Start-Infrastructure
}
