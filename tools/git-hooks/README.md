# Git Hooks for DarkAges Project

This directory contains git hooks to help maintain documentation consistency during autonomous AI agent development.

## Available Hooks

### pre-commit-reminder.py (Non-Blocking)

**Purpose:** Gently reminds agents to update status files when milestone documents are committed.

**Behavior:**
- ✅ **Non-blocking** - Never prevents commits
- ✅ **Smart detection** - Only triggers on milestone file changes
- ✅ **Gentle reminder** - Shows suggestion without enforcement
- ✅ **Autonomous-friendly** - Doesn't require human intervention

**Triggers On:**
- `WP-*-COMPLETION-REPORT.md`
- `WP-*-IMPLEMENTATION-SUMMARY.md`
- `PHASE*_COMPLETION_*.md`
- `PHASE*_SIGNOFF.md`
- `*_COMPLETE.md`

**Reminds to Update:**
- `CURRENT_STATUS.md`
- `README.md`

## Installation

### Option 1: Manual Installation (Recommended)

```bash
# Copy the hook to .git/hooks/
cp tools/git-hooks/pre-commit-reminder.py .git/hooks/pre-commit

# Make it executable (Linux/Mac)
chmod +x .git/hooks/pre-commit

# On Windows with Git Bash
chmod +x .git/hooks/pre-commit
```

### Option 2: Symlink (for development)

```bash
# Create symlink so updates to the hook automatically apply
cd .git/hooks
ln -s ../../tools/git-hooks/pre-commit-reminder.py pre-commit
chmod +x pre-commit
```

### Option 3: Automated Setup Script

```powershell
# Windows PowerShell
.\tools\git-hooks\install-hooks.ps1
```

```bash
# Linux/Mac
./tools/git-hooks/install-hooks.sh
```

## Testing the Hook

```bash
# Test without actually committing
python tools/git-hooks/pre-commit-reminder.py

# Stage a milestone file and see the reminder
git add WP-8-1-COMPLETION-REPORT.md
git commit -m "test"
# Should show reminder but still allow commit
```

## Design Philosophy

These hooks are designed for **autonomous AI agent workflows**:

1. **Never Block Work** - All hooks are informational only
2. **Smart Detection** - Only trigger on relevant changes
3. **Clear Feedback** - Messages explain what and why
4. **Easy to Override** - Can be skipped with `--no-verify` if needed
5. **Self-Documenting** - Hooks include usage instructions

## Disabling Hooks (if needed)

To temporarily disable hooks for a single commit:

```bash
git commit --no-verify -m "your message"
```

To permanently disable:

```bash
# Remove the hook file
rm .git/hooks/pre-commit
```

## Future Hooks (Planned)

- **post-commit**: Auto-generate changelog entries
- **pre-push**: Validate all tests pass before push
- **commit-msg**: Validate commit message format ([AGENT] prefix)

## Contributing

When adding new hooks:
1. Place script in `tools/git-hooks/`
2. Make it non-blocking (exit 0 always)
3. Add clear documentation here
4. Test with autonomous agent workflow
5. Update installation scripts

---

**Last Updated:** 2026-01-30  
**Philosophy:** Support, don't restrict - enable autonomous development
