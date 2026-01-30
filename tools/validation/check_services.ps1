#!/usr/bin/env pwsh
param(
    [switch]$Continuous,
    [int]$IntervalSeconds = 5
)

$dockerPath = "C:\Program Files\Docker\Docker\resources\bin\docker.exe"
if (!(Test-Path $dockerPath)) {
    $dockerPath = "docker"
}

function Test-RedisHealth {
    try {
        $result = & $dockerPath exec darkages-redis redis-cli ping 2>&1
        return ($result -eq "PONG")
    } catch {
        return $false
    }
}

function Test-ScyllaHealth {
    try {
        $result = & $dockerPath exec darkages-scylla cqlsh -e "SELECT now() FROM system.local;" 2>&1
        return ($LASTEXITCODE -eq 0 -and $result -match "system")
    } catch {
        return $false
    }
}

function Get-ContainerStatus {
    param([string]$ContainerName)
    try {
        $status = & $dockerPath inspect -f '{{.State.Status}}' $ContainerName 2>&1
        $health = & $dockerPath inspect -f '{{.State.Health.Status}}' $ContainerName 2>&1
        return @{ Status = $status; Health = $health }
    } catch {
        return @{ Status = "not found"; Health = "unknown" }
    }
}

do {
    Clear-Host
    Write-Host "==============================================" -ForegroundColor Cyan
    Write-Host "    DarkAges Infrastructure Health Check" -ForegroundColor Cyan
    Write-Host "==============================================" -ForegroundColor Cyan
    Write-Host ""
    
    # Check Docker
    try {
        $dockerVersion = & $dockerPath --version 2>&1
        Write-Host "Docker: " -NoNewline
        Write-Host $dockerVersion -ForegroundColor Green
    } catch {
        Write-Host "Docker: Not Available" -ForegroundColor Red
        return
    }
    
    Write-Host ""
    Write-Host "Services Status:" -ForegroundColor Yellow
    Write-Host "----------------------------------------------"
    
    # Redis
    $redisContainer = Get-ContainerStatus "darkages-redis"
    Write-Host "Redis     - Container: $($redisContainer.Status) | Health: $($redisContainer.Health) | Port: 6379" -NoNewline
    if (Test-RedisHealth) {
        Write-Host " [[PASS] RESPONSIVE]" -ForegroundColor Green
    } else {
        Write-Host " [[FAIL] NOT RESPONSIVE]" -ForegroundColor Red
    }
    
    # ScyllaDB
    $scyllaContainer = Get-ContainerStatus "darkages-scylla"
    Write-Host "ScyllaDB  - Container: $($scyllaContainer.Status) | Health: $($scyllaContainer.Health) | Port: 9042" -NoNewline
    if (Test-ScyllaHealth) {
        Write-Host " [[PASS] RESPONSIVE]" -ForegroundColor Green
    } else {
        Write-Host " [[FAIL] NOT RESPONSIVE]" -ForegroundColor Red
    }
    
    Write-Host ""
    Write-Host "----------------------------------------------"
    
    if ($Continuous) {
        Write-Host "Refreshing every $IntervalSeconds seconds... (Ctrl+C to exit)" -ForegroundColor Gray
        Start-Sleep -Seconds $IntervalSeconds
    }
} while ($Continuous)

Write-Host ""
Write-Host "Use -Continuous flag for live monitoring" -ForegroundColor Gray
