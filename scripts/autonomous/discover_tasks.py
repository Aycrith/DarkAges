#!/usr/bin/env python3
"""
DarkAges Autonomous Task Discovery
Scans the codebase for improvement opportunities and outputs a prioritized task queue.
Designed to be called by Hermes cron jobs for continuous autonomous iteration.
"""
import subprocess
import re
import os
import json
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Optional

PROJECT_ROOT = Path(os.environ.get("DARKAGES_ROOT", "/root/projects/DarkAges"))

@dataclass
class Task:
    id: str
    title: str
    description: str
    priority: int  # 0=critical, 1=high, 2=medium, 3=low
    category: str  # refactor, test, fix, feature
    file: str
    line: Optional[int] = None
    effort: str = "small"  # small (<30min), medium (30m-2h), large (>2h)

def run_cmd(cmd: str) -> str:
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30, cwd=PROJECT_ROOT)
        return r.stdout
    except:
        return ""

def discover_todos() -> list[Task]:
    """Find actionable TODO/FIXME markers (skip Phase/deprecated comments)."""
    tasks = []
    output = run_cmd("grep -rn '\\bTODO\\b\\|\\bFIXME\\b\\|\\bHACK\\b' src/server/src/ src/server/include/ --include='*.cpp' --include='*.hpp' 2>/dev/null")
    for line in output.strip().split('\n'):
        if not line or 'Phase ' in line or 'deprecated' in line.lower():
            continue
        match = re.match(r'(.+?):(\d+):(.*)', line)
        if match:
            filepath, lineno, text = match.groups()
            text = text.strip()
            # Skip if it's just a comment reference like "// TODO in Phase 4"
            if re.search(r'Phase \d|future|later|maybe', text, re.I):
                continue
            tasks.append(Task(
                id=f"todo-{Path(filepath).stem}-{lineno}",
                title=f"TODO at {Path(filepath).name}:{lineno}",
                description=text,
                priority=2,
                category="fix",
                file=filepath,
                line=int(lineno),
                effort="small"
            ))
    return tasks

def discover_large_files() -> list[Task]:
    """Find files >800 lines that are refactoring candidates."""
    tasks = []
    output = run_cmd("find src/server/src/ src/server/include/ -name '*.cpp' -o -name '*.hpp' | xargs wc -l 2>/dev/null | sort -rn | head -10")
    for line in output.strip().split('\n'):
        parts = line.strip().split()
        if len(parts) == 2 and parts[0].isdigit():
            lines, filepath = int(parts[0]), parts[1]
            if lines > 800 and 'test' not in filepath.lower() and '_stub' not in filepath:
                tasks.append(Task(
                    id=f"refactor-{Path(filepath).stem}",
                    title=f"Refactor {Path(filepath).name} ({lines} lines)",
                    description=f"File is {lines} lines — consider extracting cohesive subsystems",
                    priority=3,
                    category="refactor",
                    file=filepath,
                    effort="large"
                ))
    return tasks

def discover_missing_tests() -> list[Task]:
    """Find source files without corresponding test files."""
    tasks = []
    src_files = run_cmd("find src/server/src/ -name '*.cpp' ! -name '*_stub*' ! -name '*test*' -exec basename {} .cpp \\;").strip().split('\n')
    test_files = run_cmd("find src/server/tests/ -name 'Test*.cpp' -exec basename {} .cpp \\;").strip().split('\n')
    # Normalize test names: TestFoo -> Foo
    test_bases = {re.sub(r'^Test', '', t) for t in test_files if t}
    
    for src in src_files:
        if src and src not in test_bases:
            # Check if tested indirectly (e.g., ZoneServer tested via other tests)
            indirect = run_cmd(f"grep -rl '{src}' src/server/tests/ 2>/dev/null").strip()
            if not indirect:
                tasks.append(Task(
                    id=f"test-{src}",
                    title=f"Add tests for {src}",
                    description=f"No test file found for src/server/src/**/{src}.cpp",
                    priority=2,
                    category="test",
                    file=f"src/server/src/**/{src}.cpp",
                    effort="medium"
                ))
    return tasks

def discover_raw_new() -> list[Task]:
    """Find raw 'new' usage that could use smart pointers."""
    tasks = []
    output = run_cmd("grep -rn '\\bnew \\w' src/server/src/ --include='*.cpp' | grep -v 'callback\\|void\\*\\|auto\\*\\|_stub\\|test\\|//.*new\\|implementation.*new' | head -10")
    for line in output.strip().split('\n'):
        if not line:
            continue
        match = re.match(r'(.+?):(\d+):(.*)', line)
        if match:
            filepath, lineno, text = match.groups()
            tasks.append(Task(
                id=f"smartptr-{Path(filepath).stem}-{lineno}",
                title=f"Raw new at {Path(filepath).name}:{lineno}",
                description=text.strip(),
                priority=2,
                category="fix",
                file=filepath,
                line=int(lineno),
                effort="small"
            ))
    return tasks

def discover_warnings() -> list[Task]:
    """Find compiler warnings from last build."""
    tasks = []
    build_dir = PROJECT_ROOT / "build"
    if not build_dir.exists():
        return tasks
    output = run_cmd("cmake --build build -j$(nproc) 2>&1 | grep 'warning:' | grep -v 'deps/' | sort -u | head -10")
    for line in output.strip().split('\n'):
        if not line:
            continue
        match = re.match(r'(.+?):(\d+):\d+: warning: (.*)', line)
        if match:
            filepath, lineno, msg = match.groups()
            tasks.append(Task(
                id=f"warn-{Path(filepath).stem}-{lineno}",
                title=f"Warning: {msg[:60]}",
                description=msg,
                priority=3,
                category="fix",
                file=filepath,
                line=int(lineno),
                effort="small"
            ))
    return tasks

def get_test_count() -> dict:
    """Get current test count."""
    build_dir = PROJECT_ROOT / "build"
    if not build_dir.exists():
        return {"cases": 0, "assertions": 0}
    output = run_cmd("./build/darkages_tests --list-tests 2>/dev/null | tail -5")
    # Parse from test run
    output2 = run_cmd("./build/darkages_tests 2>&1 | tail -3")
    match = re.search(r'(\d+) test cases', output2)
    cases = int(match.group(1)) if match else 0
    match2 = re.search(r'(\d+) assertions', output2)
    assertions = int(match2.group(1)) if match2 else 0
    return {"cases": cases, "assertions": assertions}

def deduplicate(tasks: list[Task]) -> list[Task]:
    """Remove duplicate task IDs."""
    seen = set()
    result = []
    for t in tasks:
        if t.id not in seen:
            seen.add(t.id)
            result.append(t)
    return result

def prioritize(tasks: list[Task]) -> list[Task]:
    """Sort by priority (lower = more important), then by effort (smaller first)."""
    effort_order = {"small": 0, "medium": 1, "large": 2}
    return sorted(tasks, key=lambda t: (t.priority, effort_order.get(t.effort, 1)))

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Discover improvement tasks in DarkAges")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--count", type=int, default=0, help="Max tasks to show (0=all)")
    parser.add_argument("--category", choices=["fix", "refactor", "test", "feature"], help="Filter by category")
    parser.add_argument("--validate", action="store_true", help="Include build+test status")
    args = parser.parse_args()

    all_tasks = []
    all_tasks.extend(discover_todos())
    all_tasks.extend(discover_large_files())
    all_tasks.extend(discover_missing_tests())
    all_tasks.extend(discover_raw_new())
    all_tasks.extend(discover_warnings())
    
    tasks = deduplicate(all_tasks)
    tasks = prioritize(tasks)
    
    if args.category:
        tasks = [t for t in tasks if t.category == args.category]
    if args.count > 0:
        tasks = tasks[:args.count]

    result = {"tasks": [asdict(t) for t in tasks]}
    
    if args.validate:
        result["test_status"] = get_test_count()
        result["build_exists"] = (PROJECT_ROOT / "build").exists()

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"=== DarkAges Task Queue ({len(tasks)} tasks) ===\n")
        for i, t in enumerate(tasks, 1):
            effort_tag = {"small": "~15m", "medium": "~1h", "large": "~3h"}.get(t.effort, "?")
            pri_tag = {0: "P0-CRITICAL", 1: "P1-HIGH", 2: "P2-MEDIUM", 3: "P3-LOW"}.get(t.priority, "?")
            print(f"{i}. [{pri_tag}] [{effort_tag}] [{t.category}]")
            print(f"   {t.title}")
            if t.line:
                print(f"   {t.file}:{t.line}")
            else:
                print(f"   {t.file}")
            print(f"   {t.description}")
            print()

if __name__ == "__main__":
    main()
