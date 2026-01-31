# Install git hooks for DarkAges project (Windows PowerShell)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$GitHooksDir = git rev-parse --git-dir
$GitHooksDir = Join-Path $GitHooksDir "hooks"

Write-Host "Installing DarkAges git hooks..." -ForegroundColor Cyan
Write-Host "Target: $GitHooksDir"

# Install pre-commit hook
$SourceHook = Join-Path $ScriptDir "pre-commit-reminder.py"
$TargetHook = Join-Path $GitHooksDir "pre-commit"

if (Test-Path $SourceHook) {
    Copy-Item $SourceHook $TargetHook -Force
    Write-Host "✅ Installed pre-commit reminder hook" -ForegroundColor Green
} else {
    Write-Host "❌ pre-commit-reminder.py not found" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "🎉 Git hooks installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "These hooks are NON-BLOCKING and will only show reminders."
Write-Host "To disable: git commit --no-verify"
Write-Host ""
