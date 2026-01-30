#!/usr/bin/env python3
"""
DarkAges MMO - Code Analysis Tool
Analyzes C++ code for compilation issues without needing a compiler.
"""

import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import List, Set, Dict

@dataclass
class Issue:
    file: str
    line: int
    severity: str  # ERROR, WARNING, INFO
    message: str
    category: str

class CodeAnalyzer:
    def __init__(self, project_root: str):
        self.root = Path(project_root)
        self.issues: List[Issue] = []
        self.include_paths = [
            self.root / "src/server/include",
            self.root / "deps/entt/single_include",
        ]
        
    def analyze_all(self):
        """Run all analysis passes"""
        print("=" * 70)
        print("DarkAges Code Analysis")
        print("=" * 70)
        print()
        
        # Find all C++ files
        cpp_files = list(self.root.rglob("src/server/**/*.cpp"))
        hpp_files = list(self.root.rglob("src/server/**/*.hpp"))
        
        print(f"Found {len(cpp_files)} .cpp files")
        print(f"Found {len(hpp_files)} .hpp files")
        print()
        
        # Run analyses
        self.check_missing_includes(cpp_files + hpp_files)
        self.check_undefined_types(cpp_files + hpp_files)
        self.check_missing_pragma_once(hpp_files)
        self.check_glm_dependency(cpp_files + hpp_files)
        self.check_entt_dependency(cpp_files + hpp_files)
        self.check_circular_includes(cpp_files + hpp_files)
        
        # Report
        self.report()
        
    def check_missing_includes(self, files: List[Path]):
        """Check for missing #include directives"""
        print("[1/6] Checking for missing includes...")
        
        # Common standard library headers that might be needed
        std_symbols = {
            'std::vector': '<vector>',
            'std::map': '<map>',
            'std::unordered_map': '<unordered_map>',
            'std::set': '<set>',
            'std::unordered_set': '<unordered_set>',
            'std::span': '<span>',
            'std::chrono': '<chrono>',
            'std::mutex': '<mutex>',
            'std::thread': '<thread>',
            'std::atomic': '<atomic>',
            'std::optional': '<optional>',
            'std::expected': '<expected>',
            'std::function': '<functional>',
            'std::string': '<string>',
            'std::string_view': '<string_view>',
            'std::unique_ptr': '<memory>',
            'std::shared_ptr': '<memory>',
            'std::make_unique': '<memory>',
            'std::make_shared': '<memory>',
            'std::memcpy': '<cstring>',
            'std::memset': '<cstring>',
            'std::sqrt': '<cmath>',
            'std::abs': '<cmath>',
            'std::sin': '<cmath>',
            'std::cos': '<cmath>',
            'std::atan2': '<cmath>',
            'uint32_t': '<cstdint>',
            'uint64_t': '<cstdint>',
            'size_t': '<cstddef>',
        }
        
        for file_path in files:
            content = file_path.read_text()
            lines = content.split('\n')
            includes = set()
            
            # Find what headers are included
            for line in lines:
                match = re.match(r'#include\s+[<"](.+)[">]', line)
                if match:
                    includes.add(match.group(1))
            
            # Check for symbols that need headers
            for symbol, header in std_symbols.items():
                if symbol in content and not any(header[1:-1] in inc for inc in includes):
                    # Find the line
                    for i, line in enumerate(lines):
                        if symbol in line and not line.strip().startswith('//'):
                            self.issues.append(Issue(
                                file=str(file_path.relative_to(self.root)),
                                line=i + 1,
                                severity="WARNING",
                                message=f"Uses {symbol} but doesn't include {header}",
                                category="missing_include"
                            ))
                            break
        
        print(f"  Found {len([i for i in self.issues if i.category == 'missing_include'])} potential missing includes")
        
    def check_undefined_types(self, files: List[Path]):
        """Check for potentially undefined types"""
        print("[2/6] Checking for undefined types...")
        
        # Types we know should be defined somewhere
        known_types = {
            'EntityID', 'Registry', 'Position', 'Velocity', 'Rotation',
            'InputState', 'CombatState', 'SpatialHash', 'Constants',
        }
        
        for file_path in files:
            content = file_path.read_text()
            
            # Check for forward declarations that might need implementation
            forward_decls = re.findall(r'class\s+(\w+);', content)
            for decl in forward_decls:
                # Check if this type is actually defined in the same file
                if f'class {decl} ' not in content and f'struct {decl} ' not in content:
                    if decl not in known_types:
                        self.issues.append(Issue(
                            file=str(file_path.relative_to(self.root)),
                            line=0,
                            severity="INFO",
                            message=f"Forward declaration of '{decl}' - ensure it's defined elsewhere",
                            category="forward_decl"
                        ))
        
        print(f"  Found {len([i for i in self.issues if i.category == 'forward_decl'])} forward declarations")
        
    def check_missing_pragma_once(self, files: List[Path]):
        """Check headers for include guards"""
        print("[3/6] Checking for include guards...")
        
        for file_path in files:
            content = file_path.read_text()
            
            if '#pragma once' not in content and '#ifndef' not in content:
                self.issues.append(Issue(
                    file=str(file_path.relative_to(self.root)),
                    line=1,
                    severity="WARNING",
                    message="Header missing include guard (#pragma once or #ifndef)",
                    category="missing_guard"
                ))
        
        print(f"  Found {len([i for i in self.issues if i.category == 'missing_guard'])} headers missing guards")
        
    def check_glm_dependency(self, files: List[Path]):
        """Check for GLM usage"""
        print("[4/6] Checking for GLM dependency...")
        
        glm_files = []
        for file_path in files:
            content = file_path.read_text()
            if 'glm::' in content or '#include <glm/' in content:
                glm_files.append(file_path.relative_to(self.root))
        
        if glm_files:
            print(f"  Found {len(glm_files)} files using GLM:")
            for f in glm_files[:5]:
                print(f"    - {f}")
            if len(glm_files) > 5:
                print(f"    ... and {len(glm_files) - 5} more")
            
            self.issues.append(Issue(
                file="CMakeLists.txt",
                line=0,
                severity="INFO",
                message=f"GLM dependency needed for {len(glm_files)} files - add to CMakeLists.txt",
                category="dependency"
            ))
        else:
            print("  No GLM usage found")
            
    def check_entt_dependency(self, files: List[Path]):
        """Check for EnTT usage and verify submodule"""
        print("[5/6] Checking for EnTT dependency...")
        
        entt_files = []
        for file_path in files:
            content = file_path.read_text()
            if 'entt::' in content or '#include <entt/' in content or '#include "entt/' in content:
                entt_files.append(file_path.relative_to(self.root))
        
        print(f"  Found {len(entt_files)} files using EnTT")
        
        # Check if submodule exists
        entt_path = self.root / "deps/entt/single_include/entt/entt.hpp"
        if not entt_path.exists():
            print(f"  ERROR: EnTT submodule not found at {entt_path}")
            self.issues.append(Issue(
                file="deps/entt",
                line=0,
                severity="ERROR",
                message="EnTT submodule missing - run: git submodule update --init",
                category="missing_dependency"
            ))
        else:
            print(f"  EnTT found at deps/entt")
            
    def check_circular_includes(self, files: List[Path]):
        """Check for potential circular includes"""
        print("[6/6] Checking for circular includes...")
        
        include_graph: Dict[str, Set[str]] = {}
        
        for file_path in files:
            content = file_path.read_text()
            includes = set()
            
            for line in content.split('\n'):
                # Match quoted includes (project headers)
                match = re.match(r'#include\s+"([^"]+)"', line)
                if match:
                    inc = match.group(1)
                    # Normalize path
                    if inc.startswith('src/'):
                        includes.add(inc)
                    else:
                        # Try to resolve relative to file
                        rel_path = file_path.parent / inc
                        if rel_path.exists():
                            includes.add(str(rel_path.relative_to(self.root)))
            
            include_graph[str(file_path.relative_to(self.root))] = includes
        
        # Simple circular check (A includes B, B includes A)
        circular = []
        for file_a, includes_a in include_graph.items():
            for inc in includes_a:
                if inc in include_graph:
                    if file_a in include_graph[inc]:
                        circular.append((file_a, inc))
        
        if circular:
            print(f"  Found {len(circular)} potential circular includes")
            for a, b in circular[:3]:
                print(f"    - {a} <-> {b}")
        else:
            print("  No circular includes detected")
            
    def report(self):
        """Print final report"""
        print()
        print("=" * 70)
        print("Analysis Summary")
        print("=" * 70)
        print()
        
        errors = [i for i in self.issues if i.severity == "ERROR"]
        warnings = [i for i in self.issues if i.severity == "WARNING"]
        infos = [i for i in self.issues if i.severity == "INFO"]
        
        if errors:
            print(f"ERRORS ({len(errors)}):")
            for issue in errors:
                print(f"  [{issue.severity}] {issue.file}:{issue.line}")
                print(f"    {issue.message}")
                print()
        
        if warnings:
            print(f"WARNINGS ({len(warnings)}):")
            for issue in warnings:
                print(f"  [{issue.severity}] {issue.file}:{issue.line}")
                print(f"    {issue.message}")
                print()
        
        if infos:
            print(f"INFO ({len(infos)}):")
            for issue in infos:
                print(f"  [{issue.severity}] {issue.file}:{issue.line}")
                print(f"    {issue.message}")
                print()
        
        if not self.issues:
            print("No issues found!")
        
        print("=" * 70)
        print(f"Total: {len(errors)} errors, {len(warnings)} warnings, {len(infos)} info")
        print("=" * 70)
        
        return len(errors) == 0

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Analyze DarkAges C++ code')
    parser.add_argument('--root', default='.', help='Project root directory')
    args = parser.parse_args()
    
    analyzer = CodeAnalyzer(args.root)
    success = analyzer.analyze_all()
    
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
