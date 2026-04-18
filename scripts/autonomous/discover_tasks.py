#!/usr/bin/env python3
"""
DarkAges Task Discovery Script
Scans the codebase for actionable improvement opportunities.
Used by cron jobs to find tasks for autonomous iteration.
"""
import subprocess
import os
import re
import json
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SRC_DIR = PROJECT_ROOT / "src"
CACHE_FILE = Path(__file__).resolve().parent / ".task_cache.json"
CACHE_TTL_SECONDS = 1800  # 30 minutes


@dataclass
class Task:
    priority: str  # P0, P1, P2, P3
    category: str  # test, refactor, fix, feature
    title: str
    description: str
    files: List[str]
    estimated_hours: float


def load_cache() -> dict:
    """Load cached task discovery results if fresh."""
    if CACHE_FILE.exists():
        try:
            import time
            data = json.loads(CACHE_FILE.read_text())
            if time.time() - data.get("timestamp", 0) < CACHE_TTL_SECONDS:
                return data
        except (json.JSONDecodeError, KeyError):
            pass
    return {}


def save_cache(tasks: List[dict]):
    """Save task discovery results to cache."""
    import time
    CACHE_FILE.write_text(json.dumps({
        "timestamp": time.time(),
        "tasks": tasks
    }, indent=2))


def find_missing_tests() -> List[Task]:
    """Find source files without corresponding test files."""
    tasks = []

    # Get all .cpp source files (excluding tests, stubs, main)
    src_files = []
    for ext in ["*.cpp"]:
        for f in SRC_DIR.rglob(ext):
            if "tests" in str(f) or "stub" in f.name.lower():
                continue
            if f.name.startswith("Test"):
                continue
            if f.name == "main.cpp":
                continue
            src_files.append(f)

    # Get all test file stems (lowercased for fuzzy matching)
    test_stems_lower = set()
    for f in SRC_DIR.rglob("Test*.cpp"):
        # "TestRedisIntegration" -> "redisintegration"
        test_stems_lower.add(f.stem.lower().replace("test", ""))

    # Source basenames to skip (covered by other test files)
    covered_by_other_tests = {
        "redismanager",  # covered by TestRedisIntegration
        "scyllamanager",  # covered by TestScyllaManager
        "main",  # no test needed
    }

    for src_file in src_files:
        base_lower = src_file.stem.lower()

        # Skip if covered by a different test file
        if base_lower in covered_by_other_tests:
            continue

        # Check for exact or partial name match in test stems
        has_test = any(
            base_lower in stem or stem in base_lower
            for stem in test_stems_lower
        )

        if not has_test:
            tasks.append(Task(
                priority="P2",
                category="test",
                title=f"Add tests for {src_file.stem}",
                description=f"No test file found for {src_file.relative_to(PROJECT_ROOT)}",
                files=[str(src_file.relative_to(PROJECT_ROOT))],
                estimated_hours=1.0
            ))

    return tasks


def find_large_files() -> List[Task]:
    """Find large files that are refactoring candidates."""
    tasks = []

    for ext in ["*.cpp", "*.hpp"]:
        for f in SRC_DIR.rglob(ext):
            if "tests" in str(f) or "proto" in str(f):
                continue
            try:
                lines = len(f.read_text().splitlines())
                if lines > 800:
                    tasks.append(Task(
                        priority="P3" if lines < 1500 else "P2",
                        category="refactor",
                        title=f"Refactor {f.name} ({lines} lines)",
                        description=f"File is {lines} lines — consider extracting cohesive subsystems",
                        files=[str(f.relative_to(PROJECT_ROOT))],
                        estimated_hours=3.0
                    ))
            except (UnicodeDecodeError, PermissionError):
                pass

    return tasks


def find_todos() -> List[Task]:
    """Find actionable TODO/FIXME markers."""
    tasks = []
    skip_patterns = re.compile(
        r'Phase \d|TODO:.*implement|TODO:.*later|#.*TODO|"TODO|SPEED_HACK|FLY_HACK|NO_CLIP',
        re.IGNORECASE
    )

    try:
        result = subprocess.run(
            ["grep", "-rn", r"TODO\|FIXME",
             str(SRC_DIR), "--include=*.cpp", "--include=*.hpp"],
            capture_output=True, text=True, timeout=30
        )

        for line in result.stdout.strip().split("\n"):
            if not line or skip_patterns.search(line):
                continue
            # Extract file and line
            parts = line.split(":", 2)
            if len(parts) >= 3:
                filepath = parts[0]
                content = parts[2].strip()
                tasks.append(Task(
                    priority="P2",
                    category="fix",
                    title=f"Address TODO in {Path(filepath).name}",
                    description=content[:200],
                    files=[str(Path(filepath).relative_to(PROJECT_ROOT))],
                    estimated_hours=0.5
                ))
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    return tasks


def main():
    import sys

    # Check cache first
    cache = load_cache()
    if cache.get("tasks"):
        print(json.dumps(cache["tasks"], indent=2))
        return

    # Discover tasks
    all_tasks = []
    all_tasks.extend(find_missing_tests())
    all_tasks.extend(find_large_files())
    all_tasks.extend(find_todos())

    # Deduplicate by title
    seen = set()
    unique_tasks = []
    for task in all_tasks:
        if task.title not in seen:
            seen.add(task.title)
            unique_tasks.append(task)

    # Sort by priority
    priority_order = {"P0": 0, "P1": 1, "P2": 2, "P3": 3}
    unique_tasks.sort(key=lambda t: priority_order.get(t.priority, 99))

    task_dicts = [asdict(t) for t in unique_tasks]
    save_cache(task_dicts)

    if "--json" in sys.argv:
        print(json.dumps(task_dicts, indent=2))
    else:
        print(f"=== DarkAges Task Queue ({len(task_dicts)} tasks) ===\n")
        for i, task in enumerate(task_dicts, 1):
            print(f"{i}. [{task['priority']}] [~{task['estimated_hours']}h] [{task['category']}]")
            print(f"   {task['title']}")
            print(f"   {', '.join(task['files'])}")
            print(f"   {task['description']}")
            print()


if __name__ == "__main__":
    main()
