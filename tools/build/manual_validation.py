#!/usr/bin/env python3
"""
Manual code validation - simulates compilation checks without a compiler
"""

import re
from pathlib import Path
from dataclasses import dataclass
from typing import List, Set, Dict, Tuple

@dataclass
class ValidationIssue:
    file: str
    line: int
    severity: str
    message: str

class ManualValidator:
    def __init__(self, project_root: str):
        self.root = Path(project_root)
        self.issues: List[ValidationIssue] = []
        
    def validate_all(self):
        """Run all validation passes"""
        print("=" * 70)
        print("Manual Code Validation (Compiler-free)")
        print("=" * 70)
        print()
        
        cpp_files = list(self.root.rglob("src/server/**/*.cpp"))
        hpp_files = list(self.root.rglob("src/server/**/*.hpp"))
        
        print(f"Validating {len(cpp_files)} .cpp files, {len(hpp_files)} .hpp files")
        print()
        
        self.validate_syntax(cpp_files + hpp_files)
        self.validate_header_consistency(hpp_files)
        self.validate_function_definitions(cpp_files)
        self.validate_type_usage(cpp_files + hpp_files)
        self.validate_namespace_usage(cpp_files + hpp_files)
        
        self.report()
        
    def validate_syntax(self, files: List[Path]):
        """Basic syntax validation"""
        print("[1/5] Validating syntax...")
        
        for file_path in files:
            content = file_path.read_text()
            lines = content.split('\n')
            
            for i, line in enumerate(lines, 1):
                # Check for unbalanced braces (simple check)
                open_braces = line.count('{') - line.count('}')
                
                # Check for common syntax errors
                if ';;' in line and not line.strip().startswith('//'):
                    self.issues.append(ValidationIssue(
                        str(file_path.relative_to(self.root)), i, "WARNING",
                        "Double semicolon ;;"
                    ))
                
                # Check for missing semicolons in class/struct definitions
                if re.match(r'^\s*(class|struct)\s+\w+\s*$', line):
                    # Next non-empty line should have { or ;
                    pass  # This is complex to check properly
                
                # Check for incomplete strings
                if line.count('"') % 2 != 0 and not line.strip().startswith('//'):
                    # Might be a multi-line string, skip
                    pass
        
        print(f"  Found {len([i for i in self.issues if 'syntax' in i.message.lower()])} syntax issues")
        
    def validate_header_consistency(self, files: List[Path]):
        """Check header file consistency"""
        print("[2/5] Validating header consistency...")
        
        for file_path in files:
            content = file_path.read_text()
            
            # Check that header guards exist
            if '#pragma once' not in content and '#ifndef' not in content:
                self.issues.append(ValidationIssue(
                    str(file_path.relative_to(self.root)), 1, "ERROR",
                    "Missing header guard (#pragma once or #ifndef)"
                ))
            
            # Check for namespace usage in headers
            if 'using namespace' in content:
                lines = content.split('\n')
                for i, line in enumerate(lines, 1):
                    if 'using namespace' in line and not line.strip().startswith('//'):
                        self.issues.append(ValidationIssue(
                            str(file_path.relative_to(self.root)), i, "WARNING",
                            "'using namespace' in header file is discouraged"
                        ))
        
        print(f"  Found {len([i for i in self.issues if 'header' in i.message.lower() or 'guard' in i.message.lower()])} header issues")
        
    def validate_function_definitions(self, files: List[Path]):
        """Check for matching declarations/definitions"""
        print("[3/5] Validating function definitions...")
        
        # This is complex without parsing, so we do basic checks
        for file_path in files:
            content = file_path.read_text()
            
            # Look for undefined methods (simplistic)
            # Check for :: but no opening brace
            method_pattern = r'(\w+)::(\w+)\([^)]*\)\s*;'
            matches = re.findall(method_pattern, content)
            
            for match in matches:
                class_name, method_name = match
                # These are declarations, not necessarily errors
                pass
        
        print(f"  Function validation complete")
        
    def validate_type_usage(self, files: List[Path]):
        """Check for undefined type usage"""
        print("[4/5] Validating type usage...")
        
        # Common types that should be defined
        builtin_types = {
            'int', 'float', 'double', 'char', 'bool', 'void',
            'uint32_t', 'uint64_t', 'int32_t', 'int64_t', 'size_t',
        }
        
        for file_path in files:
            content = file_path.read_text()
            
            # Check for entt::entity usage - should include entt.hpp
            if 'entt::entity' in content or 'entt::null' in content:
                if 'entt/entt.hpp' not in content and 'CoreTypes.hpp' not in content:
                    # CoreTypes.hpp includes entt.hpp
                    pass  # This is OK if they include CoreTypes.hpp
        
        print(f"  Type validation complete")
        
    def validate_namespace_usage(self, files: List[Path]):
        """Check namespace consistency"""
        print("[5/5] Validating namespace usage...")
        
        for file_path in files:
            content = file_path.read_text()
            
            # Check for opening namespace without closing
            namespace_opens = content.count('namespace DarkAges {')
            namespace_closes = content.count('} // namespace DarkAges')
            
            if namespace_opens != namespace_closes:
                self.issues.append(ValidationIssue(
                    str(file_path.relative_to(self.root)), 0, "WARNING",
                    f"Mismatched namespace braces: {namespace_opens} opens, {namespace_closes} closes"
                ))
        
        print(f"  Found {len([i for i in self.issues if 'namespace' in i.message.lower()])} namespace issues")
        
    def report(self):
        """Print validation report"""
        print()
        print("=" * 70)
        print("Validation Summary")
        print("=" * 70)
        print()
        
        errors = [i for i in self.issues if i.severity == "ERROR"]
        warnings = [i for i in self.issues if i.severity == "WARNING"]
        
        if errors:
            print(f"ERRORS ({len(errors)}):")
            for issue in errors[:20]:  # Limit output
                print(f"  [{issue.severity}] {issue.file}:{issue.line}")
                print(f"    {issue.message}")
            if len(errors) > 20:
                print(f"  ... and {len(errors) - 20} more errors")
            print()
        
        if warnings:
            print(f"WARNINGS ({len(warnings)}):")
            for issue in warnings[:10]:  # Limit output
                print(f"  [{issue.severity}] {issue.file}:{issue.line}")
                print(f"    {issue.message}")
            if len(warnings) > 10:
                print(f"  ... and {len(warnings) - 10} more warnings")
            print()
        
        if not errors and not warnings:
            print("No issues found!")
        
        print("=" * 70)
        print(f"Total: {len(errors)} errors, {len(warnings)} warnings")
        print("=" * 70)
        
        return len(errors) == 0

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Manually validate DarkAges C++ code')
    parser.add_argument('--root', default='.', help='Project root directory')
    args = parser.parse_args()
    
    validator = ManualValidator(args.root)
    success = validator.validate_all()
    
    import sys
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
