#!/usr/bin/env python3
"""
Fix missing #include directives in C++ files
"""

import re
from pathlib import Path

# Mapping of symbols to headers they need
SYMBOL_TO_HEADER = {
    'std::vector': '<vector>',
    'std::map': '<map>',
    'std::unordered_map': '<unordered_map>',
    'std::set': '<set>',
    'std::unordered_set': '<unordered_set>',
    'std::span': '<span>',
    'std::chrono': '<chrono>',
    'std::mutex': '<mutex>',
    'std::lock_guard': '<mutex>',
    'std::unique_lock': '<mutex>',
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
    'int32_t': '<cstdint>',
    'int64_t': '<cstdint>',
    'size_t': '<cstddef>',
    'glm::': '<glm/glm.hpp>',
}

def fix_file(file_path: Path):
    """Fix missing includes in a single file"""
    content = file_path.read_text()
    original = content
    lines = content.split('\n')
    
    # Find existing includes
    existing_includes = set()
    for line in lines:
        match = re.match(r'#include\s+[<"](.+)[">]', line)
        if match:
            existing_includes.add(match.group(1))
    
    # Find what symbols are used
    needed_headers = set()
    for symbol, header in SYMBOL_TO_HEADER.items():
        if symbol in content:
            header_name = header[1:-1]  # Remove < >
            if header_name not in existing_includes:
                needed_headers.add(header)
    
    if not needed_headers:
        return False
    
    # Find where to insert (after last #include, or after #pragma once)
    insert_line = 0
    last_include_line = 0
    for i, line in enumerate(lines):
        if line.strip().startswith('#include'):
            last_include_line = i + 1
        if line.strip() == '#pragma once':
            insert_line = i + 1
    
    insert_line = max(insert_line, last_include_line)
    
    # Insert the new includes
    new_lines = lines[:insert_line]
    for header in sorted(needed_headers):
        # Check if already included with quotes instead of brackets
        header_name = header[1:-1]
        if header_name not in existing_includes and f'"{header_name}"' not in existing_includes:
            new_lines.append(f'#include {header}')
    new_lines.extend(lines[insert_line:])
    
    new_content = '\n'.join(new_lines)
    
    if new_content != original:
        file_path.write_text(new_content)
        return True
    
    return False

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Fix missing includes in DarkAges codebase')
    parser.add_argument('--root', default='.', help='Project root')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be changed')
    args = parser.parse_args()
    
    root = Path(args.root)
    files = list(root.rglob("src/server/**/*.cpp")) + list(root.rglob("src/server/**/*.hpp"))
    
    print(f"Checking {len(files)} files...")
    
    fixed = 0
    for file_path in files:
        if args.dry_run:
            # Just check if fixes would be needed
            content = file_path.read_text()
            for symbol, header in SYMBOL_TO_HEADER.items():
                if symbol in content:
                    header_name = header[1:-1]
                    if header_name not in content and f'"{header_name}"' not in content:
                        print(f"Would fix: {file_path.relative_to(root)} - needs {header}")
                        break
        else:
            if fix_file(file_path):
                print(f"Fixed: {file_path.relative_to(root)}")
                fixed += 1
    
    if not args.dry_run:
        print(f"\nFixed {fixed} files")
    
if __name__ == '__main__':
    main()
