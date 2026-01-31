#!/usr/bin/env python3
"""
Git Hook - Documentation Update Reminder (Non-Blocking)

This hook reminds agents to update CURRENT_STATUS.md when certain milestone 
files are modified, but does NOT block commits. It's designed to support 
autonomous AI agent workflows without being restrictive.

Installation:
  Copy to .git/hooks/pre-commit
  chmod +x .git/hooks/pre-commit

Design Philosophy:
  - NON-BLOCKING: Always exits 0, never prevents commits
  - GENTLE: Shows reminder message, doesn't enforce
  - SMART: Only triggers on relevant file patterns
  - AUTONOMOUS: Doesn't require human intervention
"""

import sys
import subprocess
from pathlib import Path

# Files that indicate milestone changes
MILESTONE_PATTERNS = [
    "WP-*-COMPLETION-REPORT.md",
    "WP-*-IMPLEMENTATION-SUMMARY.md",
    "PHASE*_COMPLETION_*.md",
    "PHASE*_SIGNOFF.md",
    "*_COMPLETE.md"
]

# Files that should be updated when milestones change
STATUS_FILES = [
    "CURRENT_STATUS.md",
    "README.md"
]

def get_staged_files():
    """Get list of staged files from git"""
    try:
        result = subprocess.run(
            ['git', 'diff', '--cached', '--name-only'],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip().split('\n') if result.stdout.strip() else []
    except subprocess.CalledProcessError:
        return []

def matches_pattern(filename, patterns):
    """Check if filename matches any of the patterns"""
    from fnmatch import fnmatch
    return any(fnmatch(filename, pattern) for pattern in patterns)

def main():
    """Check if status files should be updated"""
    staged_files = get_staged_files()
    
    if not staged_files:
        return 0  # No files staged, nothing to check
    
    # Check if milestone files are being committed
    milestone_changes = [
        f for f in staged_files 
        if matches_pattern(f, MILESTONE_PATTERNS)
    ]
    
    if not milestone_changes:
        return 0  # No milestone changes, no reminder needed
    
    # Check if status files are also being updated
    status_updates = [
        f for f in staged_files 
        if Path(f).name in STATUS_FILES
    ]
    
    if status_updates:
        return 0  # Status files are being updated, all good!
    
    # Show gentle reminder (but don't block)
    print("\n" + "="*70)
    print("📋 DOCUMENTATION UPDATE REMINDER (Non-Blocking)")
    print("="*70)
    print("\nYou're committing milestone changes:")
    for file in milestone_changes:
        print(f"  - {file}")
    
    print("\n💡 Consider updating these status files:")
    for file in STATUS_FILES:
        print(f"  - {file}")
    
    print("\nThis is just a reminder - your commit will proceed normally.")
    print("You can update status files in a follow-up commit if needed.")
    print("="*70 + "\n")
    
    # ALWAYS return 0 (non-blocking)
    return 0

if __name__ == "__main__":
    sys.exit(main())
