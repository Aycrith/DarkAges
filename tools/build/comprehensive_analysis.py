#!/usr/bin/env python3
"""
Comprehensive code analysis - find real issues that would cause build failures
"""

import re
from pathlib import Path
from dataclasses import dataclass
from typing import List, Tuple

@dataclass
class Issue:
    file: str
    line: int
    category: str
    message: str
    severity: str  # CRITICAL, ERROR, WARNING, INFO

def analyze_code(project_root: str = ".") -> List[Issue]:
    root = Path(project_root)
    issues = []
    
    cpp_files = list(root.rglob("src/server/**/*.cpp"))
    hpp_files = list(root.rglob("src/server/**/*.hpp"))
    
    print("=" * 70)
    print("Comprehensive Code Analysis")
    print("=" * 70)
    print()
    print(f"Analyzing {len(cpp_files)} .cpp files, {len(hpp_files)} .hpp files...")
    print()
    
    all_files = cpp_files + hpp_files
    
    for file_path in all_files:
        content = file_path.read_text()
        lines = content.split('\n')
        rel_path = str(file_path.relative_to(root))
        
        # Check 1: Brace balance in each file
        open_braces = content.count('{')
        close_braces = content.count('}')
        if open_braces != close_braces:
            issues.append(Issue(
                rel_path, 0, "BALANCE", 
                f"Unbalanced braces: {open_braces} open, {close_braces} close",
                "CRITICAL" if abs(open_braces - close_braces) > 5 else "WARNING"
            ))
        
        # Check 2: Parentheses balance
        open_parens = content.count('(')
        close_parens = content.count(')')
        if open_parens != close_parens:
            issues.append(Issue(
                rel_path, 0, "BALANCE",
                f"Unbalanced parentheses: {open_parens} open, {close_parens} close",
                "ERROR"
            ))
        
        # Check 3: Quote balance (simplistic)
        double_quotes = content.count('"') - content.count('\\"')
        if double_quotes % 2 != 0:
            issues.append(Issue(
                rel_path, 0, "BALANCE",
                "Unbalanced double quotes",
                "WARNING"
            ))
        
        # Check 4: Missing semicolons after class/struct definitions
        for i, line in enumerate(lines, 1):
            # Pattern: } followed by end of line or comment, but not semicolon
            if re.search(r'\}\s*(//.*)?$', line) and not line.strip().endswith('};'):
                # Check if previous line was a class/struct definition closing
                prev_lines = ''.join(lines[max(0, i-10):i])
                if 'class ' in prev_lines or 'struct ' in prev_lines:
                    # This might be a class definition end
                    pass  # Hard to detect accurately without parsing
        
        # Check 5: Undefined types (heuristic)
        # Look for common patterns that suggest undefined types
        undefined_patterns = [
            (r'\b(std::\w+)\b', "Potentially missing std include"),
        ]
        
        # Check 6: Check for common C++ errors
        for i, line in enumerate(lines, 1):
            # Using == instead of = in if (common mistake, might not be error)
            # if re.search(r'if\s*\([^)]*==[^)]*\)', line):
            #     pass  # This is valid, just a style check
            
            # Check for common typos
            if 'vecto' in line and 'vector' not in line:
                issues.append(Issue(rel_path, i, "TYPO", "Possible typo: 'vecto' instead of 'vector'", "WARNING"))
            
            if 'namesapce' in line:
                issues.append(Issue(rel_path, i, "TYPO", "Typo: 'namesapce' instead of 'namespace'", "CRITICAL"))
        
        # Check 7: Include what you use - basic check
        if 'std::move' in content and '<utility>' not in content:
            for i, line in enumerate(lines, 1):
                if 'std::move' in line:
                    issues.append(Issue(rel_path, i, "INCLUDE", "Using std::move without <utility>", "WARNING"))
                    break
        
        if 'std::function' in content and '<functional>' not in content:
            for i, line in enumerate(lines, 1):
                if 'std::function' in line:
                    issues.append(Issue(rel_path, i, "INCLUDE", "Using std::function without <functional>", "WARNING"))
                    break
    
    return issues

def print_report(issues: List[Issue]):
    print()
    print("=" * 70)
    print("Analysis Report")
    print("=" * 70)
    print()
    
    if not issues:
        print("No issues found!")
        print()
        print("The code appears ready for compilation.")
        return True
    
    # Group by severity
    critical = [i for i in issues if i.severity == "CRITICAL"]
    errors = [i for i in issues if i.severity == "ERROR"]
    warnings = [i for i in issues if i.severity == "WARNING"]
    info = [i for i in issues if i.severity == "INFO"]
    
    if critical:
        print(f"CRITICAL ({len(critical)}) - Will definitely fail compilation:")
        for issue in critical[:10]:
            print(f"  {issue.file}:{issue.line} [{issue.category}]")
            print(f"    {issue.message}")
        if len(critical) > 10:
            print(f"  ... and {len(critical) - 10} more")
        print()
    
    if errors:
        print(f"ERRORS ({len(errors)}) - Likely to fail compilation:")
        for issue in errors[:10]:
            print(f"  {issue.file}:{issue.line} [{issue.category}]")
            print(f"    {issue.message}")
        if len(errors) > 10:
            print(f"  ... and {len(errors) - 10} more")
        print()
    
    if warnings:
        print(f"WARNINGS ({len(warnings)}) - May cause issues:")
        for issue in warnings[:10]:
            print(f"  {issue.file}:{issue.line} [{issue.category}]")
            print(f"    {issue.message}")
        if len(warnings) > 10:
            print(f"  ... and {len(warnings) - 10} more")
        print()
    
    if info:
        print(f"INFO ({len(info)}):")
        for issue in info[:5]:
            print(f"  {issue.file}:{issue.line} [{issue.category}]")
            print(f"    {issue.message}")
        print()
    
    print("=" * 70)
    print(f"Total: {len(critical)} critical, {len(errors)} errors, {len(warnings)} warnings, {len(info)} info")
    print("=" * 70)
    
    return len(critical) == 0 and len(errors) == 0

def main():
    import sys
    import argparse
    
    parser = argparse.ArgumentParser(description='Comprehensive code analysis')
    parser.add_argument('--root', default='.', help='Project root')
    args = parser.parse_args()
    
    issues = analyze_code(args.root)
    success = print_report(issues)
    
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
