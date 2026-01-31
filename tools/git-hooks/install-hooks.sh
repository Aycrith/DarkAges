#!/usr/bin/env bash
# Install git hooks for DarkAges project (Linux/Mac)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GIT_HOOKS_DIR="$(git rev-parse --git-dir)/hooks"

echo "Installing DarkAges git hooks..."
echo "Target: $GIT_HOOKS_DIR"

# Install pre-commit hook
if [ -f "$SCRIPT_DIR/pre-commit-reminder.py" ]; then
    cp "$SCRIPT_DIR/pre-commit-reminder.py" "$GIT_HOOKS_DIR/pre-commit"
    chmod +x "$GIT_HOOKS_DIR/pre-commit"
    echo "✅ Installed pre-commit reminder hook"
else
    echo "❌ pre-commit-reminder.py not found"
    exit 1
fi

echo ""
echo "🎉 Git hooks installed successfully!"
echo ""
echo "These hooks are NON-BLOCKING and will only show reminders."
echo "To disable: git commit --no-verify"
echo ""
