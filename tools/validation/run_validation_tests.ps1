#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Master validation test runner
.DESCRIPTION
    Runs complete validation suite: setup -> services -> tests -> report
    
    This MUST pass before any Phase 8 production hardening work begins.

.PARAMETER SkipSetup
    Skip environment setup (if already done)
    
.PARAMETER SkipBuild
    Skip build step during setup
    
.PARAMETER KeepServicesRunning
    Don't stop services after tests complete
    
.PARAMETER Quick
    Run quick validation only (skip 50-player stress test)
    
.EXAMPLE
    .\run_validation_tests.ps1
    
    .\run_validation_tests.ps1 -SkipSetup -KeepServicesRunning
    
    .\run_validation_tests.ps1 -Quick
#>

param(
    [switch]$SkipSetup,
    [switch]$SkipBuild,
    [switch]$KeepServicesRunning,
    [switch]$Quick
)

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot ".." "..")
$ValidationDir = Join-Path $ProjectRoot "validation"
$LogDir = Join-Path $ProjectRoot "logs"

New-Item -ItemType Directory -Force -Path $ValidationDir | Out-Null
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

Write-Host ""
Write-Host "=" * 70
Write-Host "DarkAges MMO - Local Validation Test Suite"
Write-Host "This MUST pass before any production hardening work"
Write-Host "=" * 70
Write-Host ""

$startTime = Get-Date
$overallExitCode = 0

# Check setup status
$setupStatusFile = Join-Path $ValidationDir ".setup_status"
if (-not $SkipSetup -and (Test-Path $setupStatusFile)) {
    $setupStatus = Get-Content $setupStatusFile -Raw
    if ($setupStatus -eq "READY") {
        Write-Host "Setup already completed. Use -SkipSetup to skip." -ForegroundColor Yellow
        $response = Read-Host "Re-run setup? (y/N)"
        if ($response -ne 'y' -and $response -ne 'Y') {
            $SkipSetup = $true
        }
    }
}

# Step 1: Setup Environment
if (-not $SkipSetup) {
    Write-Host "STEP 1: Environment Setup" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "setup_environment.ps1") -SkipBuild:$SkipBuild
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] Setup failed - cannot continue" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "STEP 1: Setup (skipped)" -ForegroundColor Yellow
}

# Step 2: Start Services
Write-Host ""
Write-Host "STEP 2: Starting Services" -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "orchestrate_services.ps1") -Start
if ($LASTEXITCODE -ne 0) {
    Write-Host "[WARN]  Some services may have failed to start. Continuing with tests..." -ForegroundColor Yellow
}

# Wait for services to be fully ready
Write-Host "Waiting for services to stabilize (5s)..."
Start-Sleep -Seconds 5

# Quick check if server is responding
Write-Host "Checking server health..."
try {
    $testConnection = Test-NetConnection -ComputerName 127.0.0.1 -Port 7777 -WarningAction SilentlyContinue
    if (-not $testConnection.TcpTestSucceeded) {
        Write-Host "[WARN]  Server not responding on port 7777. Tests may fail." -ForegroundColor Yellow
    } else {
        Write-Host "[PASS] Server responding on port 7777" -ForegroundColor Green
    }
} catch {
    Write-Host "[WARN]  Could not check server status" -ForegroundColor Yellow
}

# Step 3: Run Multi-Client Tests
Write-Host ""
Write-Host "STEP 3: Running Multi-Client Tests" -ForegroundColor Cyan

Set-Location $ProjectRoot
$python = if (Get-Command "python" -ErrorAction SilentlyContinue) { "python" } else { "python3" }

# Determine which tests to run
$testArgs = @("--host", "127.0.0.1", "--port", "7777")

if ($Quick) {
    Write-Host "Quick mode: Skipping 50-player stress test" -ForegroundColor Yellow
    Write-Host "(Run without -Quick for full validation)"
    # In quick mode, we only run tests 01, 02, 04, 05
    # The Python script will need to be modified to support this
    # For now, we run all tests but they'll fail faster if services are down
}

& $python (Join-Path $PSScriptRoot "multi_client_test.py") @testArgs
$testExitCode = $LASTEXITCODE

# Step 4: Cleanup
if (-not $KeepServicesRunning) {
    Write-Host ""
    Write-Host "STEP 4: Stopping Services" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "orchestrate_services.ps1") -Stop
} else {
    Write-Host ""
    Write-Host "STEP 4: Services left running (as requested)" -ForegroundColor Yellow
    Write-Host "  Stop later with: .\tools\validation\orchestrate_services.ps1 -Stop"
}

# Step 5: Summary
Write-Host ""
Write-Host "=" * 70
$endTime = Get-Date
$duration = $endTime - $startTime

# Find the most recent validation report
$latestReport = Get-ChildItem -Path $ProjectRoot -Filter "validation_report_*.json" | 
    Sort-Object LastWriteTime -Descending | 
    Select-Object -First 1

if ($latestReport) {
    try {
        $report = Get-Content $latestReport.FullName | ConvertFrom-Json
        $passed = $report.summary.passed
        $total = $report.summary.total
        
        if ($testExitCode -eq 0 -and $passed -eq $total) {
            Write-Host "[PASS] VALIDATION PASSED" -ForegroundColor Green
            Write-Host "All tests passed. Project is ready for Phase 8: Production Hardening"
            
            # Create validation passed marker
            "PASSED" | Out-File (Join-Path $ValidationDir ".validation_status")
            
            $overallExitCode = 0
        } else {
            Write-Host "[FAIL] VALIDATION FAILED" -ForegroundColor Red
            Write-Host "Tests failed. Fix issues before proceeding to production hardening."
            
            # Create validation failed marker
            "FAILED" | Out-File (Join-Path $ValidationDir ".validation_status")
            
            $overallExitCode = 1
        }
        
        Write-Host ""
        Write-Host "Test Results:"
        foreach ($test in $report.tests) {
            $status = if ($test.passed) { "[PASS]" } else { "[FAIL]" }
            Write-Host "  $status $($test.test_name)"
        }
        
    } catch {
        Write-Host "[FAIL] Could not parse validation report" -ForegroundColor Red
        $overallExitCode = 1
    }
} else {
    if ($testExitCode -eq 0) {
        Write-Host "[PASS] VALIDATION PASSED" -ForegroundColor Green
        "PASSED" | Out-File (Join-Path $ValidationDir ".validation_status")
    } else {
        Write-Host "[FAIL] VALIDATION FAILED" -ForegroundColor Red
        "FAILED" | Out-File (Join-Path $ValidationDir ".validation_status")
        $overallExitCode = 1
    }
}

Write-Host ""
Write-Host "Duration: $($duration.ToString('hh\:mm\:ss'))"

if ($overallExitCode -ne 0) {
    Write-Host ""
    Write-Host "Check logs for details:"
    Write-Host "  - logs/setup.log"
    Write-Host "  - logs/server.log"
    Write-Host "  - validation_report_*.json"
    Write-Host "  - validation_test_*.log"
}

Write-Host "=" * 70

exit $overallExitCode
