#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Service Orchestration with Health Checks
.DESCRIPTION
    Starts and monitors all required services (Redis, ScyllaDB, Server)
    with health checks and automatic restart on failure.
#>

param(
    [switch]$Start,
    [switch]$Stop,
    [switch]$Restart,
    [switch]$Status
)

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot ".." "..")
$LogDir = Join-Path $ProjectRoot "logs"
$PidDir = Join-Path $ProjectRoot ".pids"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $PidDir | Out-Null

$Services = @{
    Redis = @{
        Name = "Redis"
        DockerImage = "redis:7-alpine"
        Port = 6379
        HealthCheck = { param($port) Test-NetConnection -ComputerName localhost -Port $port -WarningAction SilentlyContinue | Select-Object -ExpandProperty TcpTestSucceeded }
        HealthEndpoint = { "redis-cli ping" }
    }
    ScyllaDB = @{
        Name = "ScyllaDB"
        DockerImage = "scylladb/scylla:5.4"
        Port = 9042
        HealthCheck = { param($port) Test-NetConnection -ComputerName localhost -Port $port -WarningAction SilentlyContinue | Select-Object -ExpandProperty TcpTestSucceeded }
        HealthEndpoint = { "cqlsh localhost 9042 -e 'DESCRIBE KEYSPACES;'" }
    }
    DarkAgesServer = @{
        Name = "DarkAges Server"
        Binary = Join-Path $ProjectRoot "build" "Release" "darkages_server.exe"
        Port = 7777
        HealthCheck = { param($port) Test-NetConnection -ComputerName localhost -Port $port -WarningAction SilentlyContinue | Select-Object -ExpandProperty TcpTestSucceeded }
        Args = @("--port", "7777", "--redis-host", "localhost", "--scylla-host", "localhost")
    }
}

function Start-ServiceWithHealthCheck($serviceName, $maxAttempts = 30) {
    $service = $Services[$serviceName]
    Write-Host "Starting $($service.Name)..." -NoNewline
    
    switch ($serviceName) {
        "Redis" {
            docker run -d --name darkages-redis -p 6379:6379 $service.DockerImage > $null 2>&1
        }
        "ScyllaDB" {
            docker run -d --name darkages-scylla -p 9042:9042 $service.DockerImage --smp 1 --memory 1G > $null 2>&1
        }
        "DarkAgesServer" {
            if (-not (Test-Path $service.Binary)) {
                Write-Host " [FAIL] BINARY NOT FOUND"
                Write-Host "Expected: $($service.Binary)"
                return $false
            }
            $proc = Start-Process -FilePath $service.Binary -ArgumentList $service.Args -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput (Join-Path $LogDir "server.log") `
                -RedirectStandardError (Join-Path $LogDir "server_error.log")
            $proc.Id | Out-File (Join-Path $PidDir "server.pid")
        }
    }
    
    # Wait for health check
    $attempts = 0
    $healthy = $false
    while ($attempts -lt $maxAttempts -and -not $healthy) {
        Start-Sleep -Seconds 2
        $healthy = & $service.HealthCheck -port $service.Port
        $attempts++
        Write-Host "." -NoNewline
    }
    
    if ($healthy) {
        Write-Host " [PASS] HEALTHY"
        return $true
    } else {
        Write-Host " [FAIL] UNHEALTHY"
        return $false
    }
}

function Stop-AllServices() {
    Write-Host "Stopping all services..."
    
    # Stop server
    $serverPidFile = Join-Path $PidDir "server.pid"
    if (Test-Path $serverPidFile) {
        $pid = Get-Content $serverPidFile
        Stop-Process -Id $pid -Force -ErrorAction SilentlyContinue
        Remove-Item $serverPidFile
        Write-Host "  [OK] Server stopped"
    }
    
    # Stop Docker containers
    docker stop darkages-redis darkages-scylla 2>&1 | Out-Null
    docker rm darkages-redis darkages-scylla 2>&1 | Out-Null
    Write-Host "  [OK] Redis stopped"
    Write-Host "  [OK] ScyllaDB stopped"
}

function Get-ServiceStatus() {
    Write-Host "Service Status:"
    Write-Host "-" * 50
    
    foreach ($svc in $Services.GetEnumerator()) {
        $name = $svc.Value.Name
        $port = $svc.Value.Port
        $healthy = & $svc.Value.HealthCheck -port $port
        $status = if ($healthy) { "[UP] HEALTHY" } else { "[DOWN] DOWN" }
        Write-Host "$status - $name (port $port)"
    }
}

# Main
if ($Start) {
    Write-Host "=" * 70
    Write-Host "Starting DarkAges MMO Services"
    Write-Host "=" * 70
    
    $allHealthy = $true
    
    if (-not (Start-ServiceWithHealthCheck "Redis")) { $allHealthy = $false }
    if (-not (Start-ServiceWithHealthCheck "ScyllaDB")) { $allHealthy = $false }
    
    # Wait a bit for infrastructure before starting game server
    Start-Sleep -Seconds 3
    
    if (-not (Start-ServiceWithHealthCheck "DarkAgesServer")) { $allHealthy = $false }
    
    Write-Host ""
    if ($allHealthy) {
        Write-Host "[PASS] All services started and healthy!"
    } else {
        Write-Host "[WARN]  Some services failed to start healthy. Check logs for details."
    }
    Write-Host ""
    Get-ServiceStatus
}
elseif ($Stop) {
    Stop-AllServices
    Write-Host "[PASS] All services stopped"
}
elseif ($Restart) {
    Stop-AllServices
    Start-Sleep -Seconds 3
    & $PSCommandPath -Start
}
elseif ($Status) {
    Get-ServiceStatus
}
else {
    Write-Host "Usage:"
    Write-Host "  .\orchestrate_services.ps1 -Start"
    Write-Host "  .\orchestrate_services.ps1 -Stop"
    Write-Host "  .\orchestrate_services.ps1 -Restart"
    Write-Host "  .\orchestrate_services.ps1 -Status"
}
