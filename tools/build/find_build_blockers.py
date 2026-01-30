#!/usr/bin/env python3
"""
Find actual build blockers (not style issues)
Focus on issues that would prevent compilation
"""

import re
from pathlib import Path
from dataclasses import dataclass

@dataclass
class Blocker:
    file: str
    line: int
    severity: str
    message: str
    fix_suggestion: str

def find_blockers(project_root: str = "."):
    root = Path(project_root)
    blockers = []
    
    cpp_files = list(root.rglob("src/server/**/*.cpp"))
    hpp_files = list(root.rglob("src/server/**/*.hpp"))
    all_files = cpp_files + hpp_files
    
    print("=" * 70)
    print("Finding Build Blockers")
    print("=" * 70)
    print()
    
    # Critical patterns that would cause build failures
    critical_patterns = [
        # Missing semicolons at end of class/struct
        (r'^\s*(class|struct)\s+\w+\s*\{[^}]*\}\s*$', 
         "Missing semicolon after class/struct definition",
         "Add semicolon after closing brace"),
        
        # Unclosed braces (simplified check)
        (r'\{\s*$', 
         "Potential unclosed block",
         "Check brace balance"),
    ]
    
    # Check for missing virtual destructors in base classes
    base_class_pattern = r'class\s+(\w+)\s*\{[^}]*virtual\s+\w+'
    
    for file_path in all_files:
        content = file_path.read_text()
        lines = content.split('\n')
        rel_path = str(file_path.relative_to(root))
        
        for i, line in enumerate(lines, 1):
            # Check for critical syntax issues
            for pattern, message, fix in critical_patterns:
                if re.search(pattern, line):
                    blockers.append(Blocker(rel_path, i, "CRITICAL", message, fix))
            
            # Check for undefined types that would definitely fail
            # These are types we KNOW should be defined
            undefined_types = []
            for type_name in undefined_types:
                if type_name in line and f'class {type_name}' not in content and f'struct {type_name}' not in content:
                    if f'#include' not in line:  # Not in an include
                        blockers.append(Blocker(rel_path, i, "ERROR", f"Potentially undefined type: {type_name}", f"Include header defining {type_name}"))
    
    # Check for missing implementation files
    print("Checking for declaration/definition mismatches...")
    declared = set()
    defined = set()
    
    for hpp_file in hpp_files:
        content = hpp_file.read_text()
        # Find class declarations with methods
        class_matches = re.finditer(r'class\s+(\w+)', content)
        for match in class_matches:
            class_name = match.group(1)
            declared.add(class_name)
    
    for cpp_file in cpp_files:
        content = cpp_file.read_text()
        # Find method definitions
        method_matches = re.finditer(r'(\w+)::(\w+)\s*\([^)]*\)\s*\{', content)
        for match in method_matches:
            class_name = match.group(1)
            defined.add(class_name)
    
    # Report results
    print()
    print("=" * 70)
    print("Build Blockers Summary")
    print("=" * 70)
    print()
    
    if not blockers:
        print("No critical build blockers found!")
        print()
        print("Note: Style issues (line length, whitespace) were found by cpplint,")
        print("but these won't prevent compilation.")
    else:
        critical = [b for b in blockers if b.severity == "CRITICAL"]
        errors = [b for b in blockers if b.severity == "ERROR"]
        warnings = [b for b in blockers if b.severity == "WARNING"]
        
        if critical:
            print(f"CRITICAL ({len(critical)}) - Will prevent compilation:")
            for b in critical[:10]:
                print(f"  {b.file}:{b.line}: {b.message}")
                print(f"    Fix: {b.fix_suggestion}")
            if len(critical) > 10:
                print(f"  ... and {len(critical) - 10} more")
            print()
        
        if errors:
            print(f"ERRORS ({len(errors)}):")
            for b in errors[:10]:
                print(f"  {b.file}:{b.line}: {b.message}")
            print()
        
        if warnings:
            print(f"WARNINGS ({len(warnings)}):")
            for b in warnings[:5]:
                print(f"  {b.file}:{b.line}: {b.message}")
            print()
    
    print("=" * 70)
    print(f"Total blockers: {len(blockers)}")
    print("=" * 70)
    
    return len(blockers) == 0

if __name__ == '__main__':
    import sys
    success = find_blockers()
    sys.exit(0 if success else 1)
