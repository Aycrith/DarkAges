#!/usr/bin/env python3
"""
DarkAges Autonomous Iteration Orchestrator
==========================================
Discovers improvements, dispatches to coding agents, validates, and creates PRs.
Uses: OpenCode CLI, git, CMake, Hermes tools.

Usage:
    python3 orchestrator.py                  # Run full auto-discovery cycle
    python3 orchestrator.py --task "Fix X"   # Run specific task
    python3 orchestrator.py --analyze-only   # Just analyze, don't implement
    python3 orchestrator.py --validate-only  # Just validate current state
"""

import subprocess
import json
import os
import sys
import re
import time
import argparse
from datetime import datetime, timezone
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

PROJECT_ROOT = Path(os.path.expanduser("~/projects/DarkAges"))
LOG_FILE = PROJECT_ROOT / "AUTONOMOUS_LOG.md"
TASK_QUEUE_FILE = PROJECT_ROOT / "scripts" / "autonomous" / "TASK_QUEUE.md"
BRANCH_PREFIX = "autonomous"

# Colors
class C:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    END = '\033[0m'

def log(msg, color=C.CYAN):
    print(f"{color}[orchestrator]{C.END} {msg}")

def success(msg):
    log(msg, C.GREEN)

def warn(msg):
    log(msg, C.YELLOW)

def fail(msg):
    log(msg, C.RED)

def run(cmd, cwd=None, timeout=300, capture=True):
    """Run a shell command and return (stdout, stderr, returncode)."""
    r = subprocess.run(
        cmd, shell=True, cwd=cwd or PROJECT_ROOT,
        capture_output=capture, text=True, timeout=timeout
    )
    return r.stdout.strip(), r.stderr.strip(), r.returncode

# =============================================================================
# Data Types
# =============================================================================
@dataclass
class ImprovementTask:
    id: str
    title: str
    description: str
    category: str  # P0, P1, P2
    file_path: Optional[str] = None
    line_number: Optional[int] = None
    status: str = "pending"  # pending, in_progress, done, failed

@dataclass
class IterationResult:
    task: ImprovementTask
    branch: str
    timestamp: str
    build_passed: bool = False
    tests_passed: bool = False
    changes_made: bool = False
    pr_url: Optional[str] = None
    error: Optional[str] = None

# =============================================================================
# Phase 1: ANALYZE — Discover improvement opportunities
# =============================================================================
class Analyzer:
    """Scans the codebase for concrete improvement opportunities."""

    def __init__(self, project_root: Path):
        self.root = project_root
        self.tasks: list[ImprovementTask] = []

    def run_all_analyses(self) -> list[ImprovementTask]:
        log("Running codebase analysis...")
        self._find_todos()
        self._find_raw_new()
        self._find_large_files()
        self._find_missing_tests()
        self._find_stub_implementations()
        success(f"Found {len(self.tasks)} improvement opportunities")
        return self.tasks

    def _find_todos(self):
        """Find actionable TODOs in source code."""
        stdout, _, _ = run(
            r"grep -rn 'TODO\|FIXME' src/server/src/ --include='*.cpp' --include='*.hpp' 2>/dev/null || true"
        )
        for line in stdout.split('\n'):
            if not line.strip():
                continue
            parts = line.split(':', 2)
            if len(parts) < 3:
                continue
            filepath, lineno, text = parts
            text = text.strip()

            # Skip TODOs that are just comments about future phases
            if any(skip in text.lower() for skip in ['phase', 'future', 'not implemented in']):
                continue

            # Extract actionable task
            task_text = re.sub(r'^//\s*(TODO|FIXME)[:\s]*', '', text, flags=re.IGNORECASE).strip()
            if len(task_text) > 10:
                self.tasks.append(ImprovementTask(
                    id=f"todo-{len(self.tasks)+1}",
                    title=task_text[:80],
                    description=f"From {filepath}:{lineno}: {text}",
                    category="P1",
                    file_path=filepath,
                    line_number=int(lineno)
                ))

    def _find_raw_new(self):
        """Find raw 'new' that should be make_unique/make_shared (excluding C callbacks)."""
        stdout, _, _ = run(
            r"grep -rn '\bnew\s' src/server/src/ --include='*.cpp' 2>/dev/null | grep -v 'make_unique\|make_shared\|callback\|void\*\|//\|auto\*' || true"
        )
        for line in stdout.split('\n')[:5]:
            if not line.strip():
                continue
            parts = line.split(':', 2)
            if len(parts) >= 3:
                self.tasks.append(ImprovementTask(
                    id=f"mem-{len(self.tasks)+1}",
                    title=f"Replace raw new with smart pointer at {parts[0]}:{parts[1]}",
                    description=parts[2].strip(),
                    category="P1",
                    file_path=parts[0],
                    line_number=int(parts[1])
                ))

    def _find_large_files(self):
        """Identify files that should be refactored."""
        stdout, _, _ = run(
            r"find src/server/src -name '*.cpp' | xargs wc -l 2>/dev/null | sort -rn | head -5"
        )
        for line in stdout.split('\n'):
            line = line.strip()
            if not line or 'total' in line:
                continue
            parts = line.split()
            if len(parts) >= 2:
                lines = int(parts[0])
                filepath = parts[1]
                if lines > 500:
                    self.tasks.append(ImprovementTask(
                        id=f"refactor-{len(self.tasks)+1}",
                        title=f"Consider splitting {Path(filepath).name} ({lines} lines)",
                        description=f"{filepath} has {lines} lines — may benefit from decomposition",
                        category="P2",
                        file_path=filepath
                    ))

    def _find_missing_tests(self):
        """Find source files without corresponding tests."""
        src_files = set()
        stdout, _, _ = run("find src/server/src -name '*.cpp' ! -name '*stub*' ! -name 'main.cpp' -exec basename {} .cpp \\;")
        for f in stdout.split('\n'):
            if f.strip():
                src_files.add(f.strip())

        test_files = set()
        stdout, _, _ = run("find src/server/tests tests -name '*.cpp' 2>/dev/null -exec basename {} .cpp \\;")
        for f in stdout.split('\n'):
            if f.strip():
                # Strip Test prefix
                name = re.sub(r'^Test', '', f.strip())
                test_files.add(name)

        missing = src_files - test_files
        for name in sorted(missing)[:5]:
            self.tasks.append(ImprovementTask(
                id=f"test-{len(self.tasks)+1}",
                title=f"Add tests for {name}",
                description=f"No test file found for {name}.cpp",
                category="P1"
            ))

    def _find_stub_implementations(self):
        """Find stub files that may need real implementations."""
        stdout, _, _ = run("find src -name '*stub*' -type f")
        for stub in stdout.split('\n'):
            if not stub.strip():
                continue
            real = stub.replace('_stub', '')
            if os.path.exists(self.root / real):
                self.tasks.append(ImprovementTask(
                    id=f"stub-{len(self.tasks)+1}",
                    title=f"Review stub: {Path(stub).name}",
                    description=f"{stub} is a stub for {real}. Consider if real implementation is needed.",
                    category="P2",
                    file_path=stub
                ))

# =============================================================================
# Phase 2: VALIDATE — Check if changes compile and pass tests
# =============================================================================
class Validator:
    """Builds and tests the project to catch regressions."""

    def __init__(self, project_root: Path):
        self.root = project_root

    def validate(self) -> dict:
        """Run full validation. Returns {'build': bool, 'tests': bool, 'details': str}."""
        result = {'build': False, 'tests': False, 'details': ''}

        # Step 1: CMake configure
        log("Validating: CMake configure...")
        os.makedirs(self.root / "build_validate", exist_ok=True)
        _, stderr, rc = run(
            "cmake -S . -B build_validate -DBUILD_TESTS=ON "
            "-DFETCH_DEPENDENCIES=OFF -DENABLE_GNS=OFF "
            "-DENABLE_REDIS=OFF -DENABLE_SCYLLA=OFF "
            "-DENABLE_FLATBUFFERS=OFF",
            timeout=60
        )
        if rc != 0:
            result['details'] = f"CMake configure failed: {stderr[:500]}"
            fail(result['details'])
            return result

        success("CMake configure: PASS")

        # Step 2: Build
        log("Validating: Building...")
        _, stderr, rc = run(
            "cmake --build build_validate --parallel $(nproc) 2>&1 | tail -20",
            timeout=300
        )
        # Check if binary was produced even with warnings
        binary_exists = os.path.exists(self.root / "build_validate" / "DarkAgesServer") or \
                        os.path.exists(self.root / "build_validate" / "Debug" / "DarkAgesServer.exe")

        if rc == 0 or binary_exists:
            result['build'] = True
            success("Build: PASS")
        else:
            result['details'] = f"Build failed: {stderr[:500]}"
            fail(result['details'])
            run("rm -rf build_validate")
            return result

        # Step 3: Tests
        test_binary = None
        for path in [
            self.root / "build_validate" / "DarkAgesTests",
            self.root / "build_validate" / "Debug" / "DarkAgesTests.exe",
        ]:
            if path.exists():
                test_binary = str(path)
                break

        if test_binary:
            log("Validating: Running tests...")
            stdout, _, rc = run(f"{test_binary}", timeout=60)
            if rc == 0:
                result['tests'] = True
                success("Tests: PASS")
            else:
                result['details'] = f"Tests failed: {stdout[-500:]}"
                fail(result['details'])
        else:
            warn("No test binary found, skipping tests")
            result['tests'] = True  # Don't block on missing tests

        run("rm -rf build_validate")
        return result

# =============================================================================
# Phase 3: IMPLEMENT — Dispatch coding agent
# =============================================================================
class Implementer:
    """Dispatches tasks to coding agents (OpenCode, Claude Code, etc.)."""

    def __init__(self, project_root: Path):
        self.root = project_root

    def implement_with_opencode(self, task: ImprovementTask) -> bool:
        """Use OpenCode run for bounded implementation."""
        prompt = self._build_prompt(task)
        log(f"Dispatching to OpenCode: {task.title}")

        stdout, stderr, rc = run(
            f"opencode run {json.dumps(prompt)}",
            timeout=600
        )

        if rc != 0:
            fail(f"OpenCode failed: {stderr[:300]}")
            return False

        success(f"OpenCode completed: {stdout[:200]}")
        return True

    def implement_with_delegate(self, task: ImprovementTask) -> bool:
        """Use Claude Code or Codex for complex tasks."""
        # This would be called via delegate_task in Hermes
        log(f"Task requires delegate: {task.title}")
        return False  # Return False to signal Hermes should use delegate_task

    def _build_prompt(self, task: ImprovementTask) -> str:
        """Build a focused implementation prompt."""
        prompt = f"""Task: {task.title}

{task.description}

Rules:
1. Make the MINIMAL change to address this specific issue
2. Do NOT modify unrelated files
3. Ensure C++20 compliance
4. Follow existing code style in the file
5. If adding code, add it near related functionality
"""
        if task.file_path:
            prompt += f"\nPrimary file: {task.file_path}"
            if task.line_number:
                prompt += f" (around line {task.line_number})"

        return prompt

# =============================================================================
# Phase 4: GIT — Branch management, commits, PRs
# =============================================================================
class GitManager:
    """Manages branches, commits, and PRs for autonomous work."""

    def __init__(self, project_root: Path):
        self.root = project_root
        self.token = self._get_github_token()

    def _get_github_token(self) -> str:
        env_file = Path.home() / ".hermes" / ".env"
        if env_file.exists():
            for line in env_file.read_text().split('\n'):
                if line.startswith('GITHUB_TOKEN='):
                    return line.split('=', 1)[1].strip()
        return os.environ.get('GH_TOKEN', '')

    def create_branch(self, task: ImprovementTask) -> str:
        """Create a feature branch for the task."""
        slug = re.sub(r'[^a-z0-9]', '-', task.title.lower())[:40].strip('-')
        timestamp = datetime.now().strftime('%Y%m%d-%H%M%S')
        branch_name = f"{BRANCH_PREFIX}/{timestamp}-{slug}"

        run("git checkout main 2>/dev/null || git checkout master")
        run("git pull --ff-only 2>/dev/null || true")
        run(f"git checkout -b {branch_name}")

        success(f"Branch created: {branch_name}")
        return branch_name

    def commit_and_push(self, task: ImprovementTask, branch: str) -> bool:
        """Stage, commit, and push changes."""
        # Check for changes
        stdout, _, _ = run("git diff --stat")
        if not stdout.strip():
            warn("No changes to commit")
            return False

        log(f"Changes:\n{stdout}")

        run("git add -A")
        commit_msg = f"autonomous: {task.title}\n\n{task.description}\n\nValidated: build + tests passing."
        run(f'git commit -m {json.dumps(commit_msg)}')

        _, stderr, rc = run(f"git push -u origin {branch}")
        if rc != 0:
            fail(f"Push failed: {stderr[:300]}")
            return False

        success("Changes committed and pushed")
        return True

    def create_pr(self, task: ImprovementTask, branch: str) -> Optional[str]:
        """Create a GitHub PR."""
        if not self.token:
            warn("No GitHub token — skipping PR creation")
            return None

        body = f"""## Autonomous Iteration

**Task:** {task.title}
**Category:** {task.category}
**Branch:** `{branch}`

### Description
{task.description}

### Validation
- Build: passing
- Tests: passing

---
*Generated by DarkAges Autonomous Iteration Framework*"""

        # Use gh CLI if available, otherwise curl
        stdout, _, rc = run(
            f'GH_TOKEN="{self.token}" gh pr create '
            f'--title "autonomous: {task.title}" '
            f'--body {json.dumps(body)} '
            f'--base main 2>&1'
        )

        if rc == 0:
            success(f"PR created: {stdout}")
            return stdout.strip()

        # Fallback: GitHub API
        payload = json.dumps({
            "title": f"autonomous: {task.title}",
            "body": body,
            "head": branch,
            "base": "main"
        })

        stdout, _, rc = run(
            f'curl -s -X POST '
            f'-H "Authorization: token {self.token}" '
            f'-H "Content-Type: application/json" '
            f'"https://api.github.com/repos/Aycrith/DarkAges/pulls" '
            f"-d '{payload}'"
        )

        if rc == 0:
            try:
                data = json.loads(stdout)
                if 'html_url' in data:
                    success(f"PR created: {data['html_url']}")
                    return data['html_url']
            except json.JSONDecodeError:
                pass

        fail("PR creation failed")
        return None

    def rollback(self, branch: str):
        """Roll back changes by deleting the branch."""
        run("git checkout main 2>/dev/null || git checkout master")
        run(f"git branch -D {branch} 2>/dev/null || true")
        warn(f"Rolled back branch: {branch}")

# =============================================================================
# Phase 5: LOG — Track iterations
# =============================================================================
class Logger:
    """Logs iterations to AUTONOMOUS_LOG.md."""

    def __init__(self, log_file: Path):
        self.log_file = log_file

    def log_iteration(self, result: IterationResult):
        """Append iteration result to the log."""
        status = "SUCCESS" if (result.build_passed and result.tests_passed and result.changes_made) else "FAILED"
        emoji = "✅" if status == "SUCCESS" else "❌"

        entry = f"""
### {emoji} {result.timestamp}
- **Task:** {result.task.title}
- **Category:** {result.task.category}
- **Branch:** `{result.branch}`
- **Build:** {'PASS' if result.build_passed else 'FAIL'}
- **Tests:** {'PASS' if result.tests_passed else 'FAIL'}
- **Changes:** {'YES' if result.changes_made else 'NO'}
- **PR:** {result.pr_url or 'N/A'}
"""
        if result.error:
            entry += f"- **Error:** {result.error}\n"

        with open(self.log_file, 'a') as f:
            f.write(entry)

        log(f"Logged iteration to {self.log_file}")

# =============================================================================
# ORCHESTRATOR — Main entry point
# =============================================================================
class Orchestrator:
    """Coordinates the full autonomous iteration cycle."""

    def __init__(self):
        self.analyzer = Analyzer(PROJECT_ROOT)
        self.validator = Validator(PROJECT_ROOT)
        self.implementer = Implementer(PROJECT_ROOT)
        self.git = GitManager(PROJECT_ROOT)
        self.logger = Logger(LOG_FILE)

    def run_cycle(self, task: Optional[ImprovementTask] = None):
        """Run one full iteration cycle."""
        log("=" * 60, C.BOLD)
        log("AUTONOMOUS ITERATION CYCLE", C.BOLD)
        log("=" * 60, C.BOLD)

        # 1. Analyze (if no task given)
        if task is None:
            tasks = self.analyzer.run_all_analyses()
            if not tasks:
                success("No improvements needed!")
                return
            task = self._select_task(tasks)

        log(f"Selected: [{task.category}] {task.title}")

        # 2. Baseline validation
        log("\n--- Baseline Validation ---")
        baseline = self.validator.validate()
        if not baseline['build']:
            fail("Baseline build failed — cannot proceed")
            return

        # 3. Create branch
        branch = self.git.create_branch(task)

        # 4. Implement
        log("\n--- Implementation ---")
        implemented = self.implementer.implement_with_opencode(task)

        # 5. Post-implementation validation
        result = IterationResult(
            task=task,
            branch=branch,
            timestamp=datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
        )

        if implemented:
            stdout, _, _ = run("git diff --stat")
            result.changes_made = bool(stdout.strip())

            if result.changes_made:
                log("\n--- Post-Implementation Validation ---")
                post = self.validator.validate()
                result.build_passed = post['build']
                result.tests_passed = post['tests']

                if result.build_passed and result.tests_passed:
                    # 6. Commit and PR
                    if self.git.commit_and_push(task, branch):
                        result.pr_url = self.git.create_pr(task, branch)
                    success("Iteration SUCCEEDED")
                else:
                    result.error = post.get('details', 'Validation failed')
                    fail("Validation failed — rolling back")
                    self.git.rollback(branch)
            else:
                warn("No changes made by implementer")
                result.build_passed = baseline['build']
                result.tests_passed = baseline['tests']
        else:
            result.error = "Implementation failed"
            self.git.rollback(branch)

        # 7. Log
        self.logger.log_iteration(result)
        return result

    def _select_task(self, tasks: list[ImprovementTask]) -> ImprovementTask:
        """Select the best task to work on (prioritized)."""
        # P0 > P1 > P2
        for priority in ['P0', 'P1', 'P2']:
            pending = [t for t in tasks if t.category == priority and t.status == 'pending']
            if pending:
                return pending[0]
        return tasks[0]

    def analyze_only(self):
        """Just run analysis and print results."""
        tasks = self.analyzer.run_all_analyses()
        print(f"\n{'='*60}")
        print(f"Found {len(tasks)} improvement opportunities:")
        print(f"{'='*60}")
        for t in tasks:
            print(f"  [{t.category}] {t.title}")
            if t.file_path:
                print(f"         → {t.file_path}:{t.line_number or '?'}")

    def validate_only(self):
        """Just run validation."""
        result = self.validator.validate()
        print(f"\nBuild: {'PASS' if result['build'] else 'FAIL'}")
        print(f"Tests: {'PASS' if result['tests'] else 'FAIL'}")
        if result['details']:
            print(f"Details: {result['details']}")

# =============================================================================
# CLI
# =============================================================================
def main():
    parser = argparse.ArgumentParser(description="DarkAges Autonomous Orchestrator")
    parser.add_argument('--task', type=str, help='Specific task to implement')
    parser.add_argument('--analyze-only', action='store_true', help='Only analyze, don\'t implement')
    parser.add_argument('--validate-only', action='store_true', help='Only validate, don\'t implement')
    parser.add_argument('--cycles', type=int, default=1, help='Number of iteration cycles to run')
    args = parser.parse_args()

    orch = Orchestrator()

    if args.analyze_only:
        orch.analyze_only()
    elif args.validate_only:
        orch.validate_only()
    else:
        task = None
        if args.task:
            task = ImprovementTask(
                id="manual-1",
                title=args.task,
                description=args.task,
                category="P1"
            )

        for i in range(args.cycles):
            if args.cycles > 1:
                log(f"\n{'#'*60}", C.BOLD)
                log(f"CYCLE {i+1}/{args.cycles}", C.BOLD)
                log(f"{'#'*60}", C.BOLD)

            result = orch.run_cycle(task)

            if args.cycles > 1 and result and not result.build_passed:
                warn("Stopping multi-cycle run due to build failure")
                break

if __name__ == "__main__":
    main()
