#!/usr/bin/env python3
"""
DarkAges MMO - Version Bumping Script

Usage:
    python version_bump.py [major|minor|patch] [--file VERSION]
    python version_bump.py --set 1.2.3 [--file VERSION]
    python version_bump.py --show [--file VERSION]

Examples:
    python version_bump.py patch    # 0.7.0 -> 0.7.1
    python version_bump.py minor    # 0.7.0 -> 0.8.0
    python version_bump.py major    # 0.7.0 -> 1.0.0
    python version_bump.py --set 1.0.0  # Set version directly
"""

import argparse
import re
import sys
from pathlib import Path
from datetime import datetime


def read_version(version_file: Path) -> str:
    """Read version from file."""
    if not version_file.exists():
        return "0.1.0"
    
    content = version_file.read_text().strip()
    # Extract version number (handles formats like "v1.0.0" or "1.0.0")
    match = re.search(r'(\d+)\.(\d+)\.(\d+)', content)
    if match:
        return f"{match.group(1)}.{match.group(2)}.{match.group(3)}"
    return "0.1.0"


def write_version(version_file: Path, version: str) -> None:
    """Write version to file."""
    version_file.write_text(f"{version}\n")


def bump_version(version: str, bump_type: str) -> str:
    """Bump version according to type."""
    parts = version.split('.')
    if len(parts) != 3:
        raise ValueError(f"Invalid version format: {version}. Expected: x.y.z")
    
    try:
        major, minor, patch = int(parts[0]), int(parts[1]), int(parts[2])
    except ValueError:
        raise ValueError(f"Invalid version format: {version}. Expected: x.y.z")
    
    if bump_type == 'major':
        major += 1
        minor = 0
        patch = 0
    elif bump_type == 'minor':
        minor += 1
        patch = 0
    elif bump_type == 'patch':
        patch += 1
    else:
        raise ValueError(f"Unknown bump type: {bump_type}")
    
    return f"{major}.{minor}.{patch}"


def validate_version(version: str) -> bool:
    """Validate semantic version format."""
    pattern = r'^(\d+)\.(\d+)\.(\d+)(?:-[a-zA-Z0-9.]+)?$'
    return re.match(pattern, version) is not None


def update_cmake_version(project_root: Path, version: str) -> None:
    """Update version in CMakeLists.txt if present."""
    cmake_file = project_root / "CMakeLists.txt"
    if not cmake_file.exists():
        return
    
    content = cmake_file.read_text()
    
    # Update project version
    pattern = r'(project\s*\(\s*[^)]+VERSION\s+)\d+\.\d+\.\d+'
    if re.search(pattern, content):
        new_content = re.sub(pattern, rf'\g<1>{version}', content)
        cmake_file.write_text(new_content)
        print(f"  Updated {cmake_file}")


def update_client_version(project_root: Path, version: str) -> None:
    """Update version in Godot project file if present."""
    project_file = project_root / "src" / "client" / "project.godot"
    if not project_file.exists():
        return
    
    content = project_file.read_text()
    
    # Update config/version
    pattern = r'(config/version=)"[^"]*"'
    if re.search(pattern, content):
        new_content = re.sub(pattern, rf'\g<1>"{version}"', content)
        project_file.write_text(new_content)
        print(f"  Updated {project_file}")


def update_csproj_version(project_root: Path, version: str) -> None:
    """Update version in C# project file if present."""
    csproj_files = list((project_root / "src" / "client").glob("*.csproj"))
    
    for csproj_file in csproj_files:
        content = csproj_file.read_text()
        
        # Update Version elements
        patterns = [
            (r'(<Version>)\d+\.\d+\.\d+(</Version>)', rf'\g<1>{version}\g<2>'),
            (r'(<AssemblyVersion>)\d+\.\d+\.\d+(</AssemblyVersion>)', rf'\g<1>{version}\g<2>'),
            (r'(<FileVersion>)\d+\.\d+\.\d+(</FileVersion>)', rf'\g<1>{version}\g<2>'),
        ]
        
        new_content = content
        for pattern, replacement in patterns:
            new_content = re.sub(pattern, replacement, new_content)
        
        if new_content != content:
            csproj_file.write_text(new_content)
            print(f"  Updated {csproj_file}")


def generate_changelog_entry(version: str, bump_type: str) -> str:
    """Generate a changelog entry template."""
    date_str = datetime.now().strftime("%Y-%m-%d")
    
    return f"""## [{version}] - {date_str}

### {bump_type.capitalize()} Release

#### Added
- 

#### Changed
- 

#### Fixed
- 

#### Removed
- 

---

"""


def update_changelog(project_root: Path, version: str, bump_type: str) -> None:
    """Update CHANGELOG.md with new version entry."""
    changelog_file = project_root / "CHANGELOG.md"
    
    entry = generate_changelog_entry(version, bump_type)
    
    if changelog_file.exists():
        content = changelog_file.read_text()
        # Insert after the header
        lines = content.split('\n')
        # Find the first ## line (usually the first version entry)
        insert_idx = 0
        for i, line in enumerate(lines):
            if line.startswith('## ['):
                insert_idx = i
                break
        
        lines.insert(insert_idx, entry.rstrip())
        changelog_file.write_text('\n'.join(lines))
    else:
        # Create new changelog
        header = """# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

"""
        changelog_file.write_text(header + entry)
    
    print(f"  Updated {changelog_file}")


def create_git_tag(version: str, message: str = None) -> None:
    """Create a git tag for the version."""
    import subprocess
    
    tag_name = f"v{version}"
    
    if message is None:
        message = f"Release version {version}"
    
    try:
        subprocess.run(
            ["git", "tag", "-a", tag_name, "-m", message],
            check=True,
            capture_output=True
        )
        print(f"  Created git tag: {tag_name}")
    except subprocess.CalledProcessError as e:
        print(f"  Warning: Failed to create git tag: {e}")
    except FileNotFoundError:
        print("  Warning: Git not found, skipping tag creation")


def main():
    parser = argparse.ArgumentParser(
        description="DarkAges MMO Version Bumping Script",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s patch              # Bump patch version: 0.7.0 -> 0.7.1
  %(prog)s minor              # Bump minor version: 0.7.0 -> 0.8.0
  %(prog)s major              # Bump major version: 0.7.0 -> 1.0.0
  %(prog)s --set 1.0.0        # Set version directly
  %(prog)s --show             # Show current version
        """
    )
    
    parser.add_argument(
        'type',
        nargs='?',
        choices=['major', 'minor', 'patch'],
        help='Type of version bump'
    )
    
    parser.add_argument(
        '--file',
        default='VERSION',
        help='Path to version file (default: VERSION)'
    )
    
    parser.add_argument(
        '--set',
        metavar='VERSION',
        help='Set version directly instead of bumping'
    )
    
    parser.add_argument(
        '--show',
        action='store_true',
        help='Show current version and exit'
    )
    
    parser.add_argument(
        '--update-all',
        action='store_true',
        default=True,
        help='Update version in all project files (default: True)'
    )
    
    parser.add_argument(
        '--no-update-all',
        dest='update_all',
        action='store_false',
        help='Only update VERSION file, not other project files'
    )
    
    parser.add_argument(
        '--changelog',
        action='store_true',
        help='Update CHANGELOG.md with new version entry'
    )
    
    parser.add_argument(
        '--tag',
        action='store_true',
        help='Create a git tag for the new version'
    )
    
    parser.add_argument(
        '--tag-message',
        help='Message for the git tag'
    )
    
    args = parser.parse_args()
    
    # Find project root
    script_dir = Path(__file__).parent.resolve()
    project_root = script_dir.parent.parent
    version_file = project_root / args.file
    
    # Show current version
    if args.show:
        version = read_version(version_file)
        print(version)
        return 0
    
    # Validate arguments
    if args.set:
        if not validate_version(args.set):
            print(f"Error: Invalid version format: {args.set}", file=sys.stderr)
            print("Expected format: x.y.z (e.g., 1.0.0)", file=sys.stderr)
            return 1
        new_version = args.set
        bump_type = "set"
    elif args.type:
        current_version = read_version(version_file)
        new_version = bump_version(current_version, args.type)
        bump_type = args.type
    else:
        parser.print_help()
        return 1
    
    # Read current version for display
    current_version = read_version(version_file)
    
    # Write new version
    write_version(version_file, new_version)
    print(f"Version: {current_version} -> {new_version}")
    
    # Update other project files
    if args.update_all:
        print("\nUpdating project files:")
        update_cmake_version(project_root, new_version)
        update_client_version(project_root, new_version)
        update_csproj_version(project_root, new_version)
    
    # Update changelog
    if args.changelog:
        print("\nUpdating changelog:")
        update_changelog(project_root, new_version, bump_type)
    
    # Create git tag
    if args.tag:
        print("\nCreating git tag:")
        create_git_tag(new_version, args.tag_message)
    
    print(f"\n✓ Version bumped to {new_version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
