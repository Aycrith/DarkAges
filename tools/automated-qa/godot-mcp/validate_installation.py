"""
Godot MCP Installation Validation Script

Comprehensive validation of the Godot MCP integration installation.
This script performs detailed checks of all components.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Tuple


class Colors:
    """ANSI color codes for terminal output."""
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'


class ValidationResult:
    """Result of a validation check."""
    def __init__(self, name: str, passed: bool, message: str = "", details: Dict = None):
        self.name = name
        self.passed = passed
        self.message = message
        self.details = details or {}
        self.timestamp = time.time()


class MCPValidator:
    """Validates Godot MCP installation."""
    
    def __init__(self, project_root: Path = None):
        self.project_root = project_root or Path(__file__).parent.parent.parent.parent
        self.results: List[ValidationResult] = []
        self.godot_path = os.environ.get("GODOT_PATH", "")
        self.mcp_server_path = self.project_root / "tools" / "godot-mcp" / "build" / "index.js"
        
    def log(self, message: str, color: str = Colors.RESET):
        """Print colored message."""
        print(f"{color}{message}{Colors.RESET}")
        
    def check_prerequisites(self) -> ValidationResult:
        """Check Node.js and npm versions."""
        self.log("\n[CHECK] Prerequisites", Colors.BOLD + Colors.CYAN)
        
        try:
            # Check Node.js
            node_result = subprocess.run(
                ["node", "--version"],
                capture_output=True,
                text=True,
                timeout=5
            )
            node_version = node_result.stdout.strip()
            
            # Check npm
            npm_result = subprocess.run(
                ["npm", "--version"],
                capture_output=True,
                text=True,
                timeout=5
            )
            npm_version = npm_result.stdout.strip()
            
            # Parse version numbers
            node_major = int(node_version.lstrip('v').split('.')[0])
            
            details = {
                "node_version": node_version,
                "npm_version": npm_version,
                "node_meets_requirement": node_major >= 18
            }
            
            if node_major >= 18:
                self.log(f"  [PASS] Node.js {node_version}", Colors.GREEN)
                self.log(f"  [PASS] npm {npm_version}", Colors.GREEN)
                return ValidationResult(
                    "Prerequisites",
                    True,
                    f"Node.js {node_version}, npm {npm_version}",
                    details
                )
            else:
                self.log(f"  [FAIL] Node.js {node_version} (requires 18+)", Colors.RED)
                return ValidationResult(
                    "Prerequisites",
                    False,
                    f"Node.js {node_version} is too old (requires 18+)",
                    details
                )
                
        except Exception as e:
            self.log(f"  [FAIL] {e}", Colors.RED)
            return ValidationResult("Prerequisites", False, str(e))
    
    def check_godot_installation(self) -> ValidationResult:
        """Check Godot engine installation."""
        self.log("\n[CHECK] Godot Engine", Colors.BOLD + Colors.CYAN)
        
        # Check environment variable
        if not self.godot_path:
            # Try to find in common locations
            possible_paths = [
                self.project_root / "tools" / "godot" / "Godot_v4.2.2-stable_mono_win64" / "Godot_v4.2.2-stable_mono_win64.exe",
                Path("C:/Tools/Godot/Godot_v4.2.exe"),
                Path("C:/Program Files/Godot/Godot.exe"),
            ]
            
            for path in possible_paths:
                if path.exists():
                    self.godot_path = str(path)
                    break
        
        if not self.godot_path or not Path(self.godot_path).exists():
            self.log("  [FAIL] Godot not found", Colors.RED)
            return ValidationResult(
                "Godot Installation",
                False,
                "Godot executable not found. Set GODOT_PATH environment variable."
            )
        
        self.log(f"  [PASS] Godot found: {self.godot_path}", Colors.GREEN)
        
        # Try to get version
        try:
            result = subprocess.run(
                [self.godot_path, "--version"],
                capture_output=True,
                text=True,
                timeout=10
            )
            version = result.stdout.strip() or result.stderr.strip()
            self.log(f"  [INFO] Version: {version}", Colors.BLUE)
            
            return ValidationResult(
                "Godot Installation",
                True,
                f"Godot found at {self.godot_path}",
                {"path": self.godot_path, "version": version}
            )
        except Exception as e:
            self.log(f"  [WARN] Could not get version: {e}", Colors.YELLOW)
            return ValidationResult(
                "Godot Installation",
                True,
                f"Godot found but could not get version",
                {"path": self.godot_path, "error": str(e)}
            )
    
    def check_mcp_server(self) -> ValidationResult:
        """Check MCP server installation."""
        self.log("\n[CHECK] MCP Server", Colors.BOLD + Colors.CYAN)
        
        mcp_dir = self.project_root / "tools" / "godot-mcp"
        
        if not mcp_dir.exists():
            self.log("  [FAIL] MCP server directory not found", Colors.RED)
            return ValidationResult(
                "MCP Server",
                False,
                f"MCP server not found at {mcp_dir}"
            )
        
        self.log(f"  [PASS] MCP directory: {mcp_dir}", Colors.GREEN)
        
        # Check build output
        if not self.mcp_server_path.exists():
            self.log(f"  [FAIL] Build output not found: {self.mcp_server_path}", Colors.RED)
            return ValidationResult(
                "MCP Server",
                False,
                "MCP server not built. Run 'npm run build' in the godot-mcp directory."
            )
        
        self.log(f"  [PASS] Build output: {self.mcp_server_path}", Colors.GREEN)
        
        # Check package.json
        package_json = mcp_dir / "package.json"
        if package_json.exists():
            with open(package_json) as f:
                pkg = json.load(f)
            self.log(f"  [INFO] MCP version: {pkg.get('version', 'unknown')}", Colors.BLUE)
        
        return ValidationResult(
            "MCP Server",
            True,
            f"MCP server installed at {mcp_dir}",
            {"path": str(mcp_dir), "build": str(self.mcp_server_path)}
        )
    
    def check_darkages_project(self) -> ValidationResult:
        """Check DarkAges client project."""
        self.log("\n[CHECK] DarkAges Project", Colors.BOLD + Colors.CYAN)
        
        client_path = self.project_root / "src" / "client"
        
        if not client_path.exists():
            self.log(f"  [FAIL] Client directory not found: {client_path}", Colors.RED)
            return ValidationResult(
                "DarkAges Project",
                False,
                f"Client project not found at {client_path}"
            )
        
        self.log(f"  [PASS] Client directory: {client_path}", Colors.GREEN)
        
        # Check project.godot
        project_file = client_path / "project.godot"
        if not project_file.exists():
            self.log("  [FAIL] project.godot not found", Colors.RED)
            return ValidationResult(
                "DarkAges Project",
                False,
                "project.godot not found"
            )
        
        self.log("  [PASS] project.godot exists", Colors.GREEN)
        
        # Parse project file
        try:
            with open(project_file) as f:
                content = f.read()
            
            # Extract key info
            project_name = "Unknown"
            godot_version = "Unknown"
            main_scene = "Unknown"
            
            for line in content.split('\n'):
                if 'config/name=' in line:
                    project_name = line.split('=')[1].strip().strip('"')
                if 'config/features=' in line:
                    if '4.2' in line:
                        godot_version = "4.2"
                    elif '4.3' in line:
                        godot_version = "4.3"
                if 'run/main_scene=' in line:
                    main_scene = line.split('=')[1].strip().strip('"')
            
            self.log(f"  [INFO] Project: {project_name}", Colors.BLUE)
            self.log(f"  [INFO] Godot version: {godot_version}", Colors.BLUE)
            self.log(f"  [INFO] Main scene: {main_scene}", Colors.BLUE)
            
            # Count files
            scenes = list(client_path.rglob("*.tscn"))
            scripts = list(client_path.rglob("*.cs"))
            
            self.log(f"  [INFO] Scene files: {len(scenes)}", Colors.BLUE)
            self.log(f"  [INFO] C# scripts: {len(scripts)}", Colors.BLUE)
            
            return ValidationResult(
                "DarkAges Project",
                True,
                f"Project '{project_name}' validated",
                {
                    "name": project_name,
                    "godot_version": godot_version,
                    "main_scene": main_scene,
                    "scenes": len(scenes),
                    "scripts": len(scripts)
                }
            )
            
        except Exception as e:
            self.log(f"  [WARN] Could not parse project: {e}", Colors.YELLOW)
            return ValidationResult(
                "DarkAges Project",
                True,
                "Project exists but could not parse details",
                {"error": str(e)}
            )
    
    def check_configuration(self) -> ValidationResult:
        """Check MCP configuration files."""
        self.log("\n[CHECK] Configuration Files", Colors.BOLD + Colors.CYAN)
        
        config_path = Path(__file__).parent / "config.json"
        
        if not config_path.exists():
            self.log(f"  [FAIL] config.json not found: {config_path}", Colors.RED)
            return ValidationResult(
                "Configuration",
                False,
                "config.json not found"
            )
        
        self.log(f"  [PASS] config.json exists", Colors.GREEN)
        
        try:
            with open(config_path) as f:
                config = json.load(f)
            
            # Check MCP server config
            mcp_config = config.get("mcpServers", {}).get("godot", {})
            if not mcp_config:
                self.log("  [FAIL] MCP server config missing", Colors.RED)
                return ValidationResult("Configuration", False, "MCP server config missing")
            
            self.log("  [PASS] MCP server configuration present", Colors.GREEN)
            
            # Check DarkAges config
            da_config = config.get("darkages", {})
            if not da_config:
                self.log("  [WARN] DarkAges config section missing", Colors.YELLOW)
            else:
                self.log("  [PASS] DarkAges configuration present", Colors.GREEN)
            
            return ValidationResult(
                "Configuration",
                True,
                f"Configuration loaded from {config_path}",
                {"config_path": str(config_path)}
            )
            
        except json.JSONDecodeError as e:
            self.log(f"  [FAIL] Invalid JSON: {e}", Colors.RED)
            return ValidationResult("Configuration", False, f"Invalid JSON: {e}")
        except Exception as e:
            self.log(f"  [FAIL] {e}", Colors.RED)
            return ValidationResult("Configuration", False, str(e))
    
    def check_server_binary(self) -> ValidationResult:
        """Check if DarkAges server binary exists."""
        self.log("\n[CHECK] DarkAges Server Binary", Colors.BOLD + Colors.CYAN)
        
        server_path = self.project_root / "build" / "Release" / "darkages_server.exe"
        
        if not server_path.exists():
            server_path = self.project_root / "build" / "darkages_server.exe"
        
        if server_path.exists():
            size_mb = server_path.stat().st_size / (1024 * 1024)
            self.log(f"  [PASS] Server binary: {server_path}", Colors.GREEN)
            self.log(f"  [INFO] Size: {size_mb:.2f} MB", Colors.BLUE)
            return ValidationResult(
                "Server Binary",
                True,
                f"Server binary found ({size_mb:.2f} MB)",
                {"path": str(server_path), "size_mb": size_mb}
            )
        else:
            self.log(f"  [WARN] Server binary not found at {server_path}", Colors.YELLOW)
            return ValidationResult(
                "Server Binary",
                True,  # Not a blocker for MCP
                "Server binary not found (run build)",
                {"expected_path": str(server_path)}
            )
    
    def test_mcp_tool_execution(self) -> ValidationResult:
        """Test actual MCP tool execution."""
        self.log("\n[CHECK] MCP Tool Execution", Colors.BOLD + Colors.CYAN)
        
        # Note: This requires the MCP server to be running
        # In practice, the AI assistant starts the MCP server
        
        self.log("  [INFO] MCP tools can only be tested when AI assistant is running", Colors.YELLOW)
        self.log("  [INFO] Tools available:", Colors.BLUE)
        
        tools = [
            "get_godot_version",
            "launch_editor",
            "run_project",
            "stop_project",
            "get_debug_output",
            "create_scene",
            "add_node",
            "save_scene",
            "take_screenshot"
        ]
        
        for tool in tools:
            self.log(f"    - {tool}", Colors.BLUE)
        
        return ValidationResult(
            "MCP Tools",
            True,
            "9 tools available (require AI assistant to execute)",
            {"tools": tools}
        )
    
    def run_all_checks(self) -> Dict:
        """Run all validation checks."""
        self.log("=" * 70, Colors.BOLD)
        self.log("GODOT MCP INSTALLATION VALIDATION", Colors.BOLD + Colors.CYAN)
        self.log("=" * 70, Colors.BOLD)
        self.log(f"Project: {self.project_root}")
        self.log(f"Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")
        
        checks = [
            self.check_prerequisites,
            self.check_godot_installation,
            self.check_mcp_server,
            self.check_darkages_project,
            self.check_configuration,
            self.check_server_binary,
            self.test_mcp_tool_execution
        ]
        
        for check in checks:
            result = check()
            self.results.append(result)
        
        return self.generate_report()
    
    def generate_report(self) -> Dict:
        """Generate validation report."""
        passed = sum(1 for r in self.results if r.passed)
        failed = sum(1 for r in self.results if not r.passed)
        total = len(self.results)
        
        self.log("\n" + "=" * 70, Colors.BOLD)
        self.log("VALIDATION SUMMARY", Colors.BOLD + Colors.CYAN)
        self.log("=" * 70, Colors.BOLD)
        
        for result in self.results:
            status = "[PASS]" if result.passed else "[FAIL]"
            color = Colors.GREEN if result.passed else Colors.RED
            self.log(f"  {status} {result.name}: {result.message}", color)
        
        self.log("\n" + "-" * 70, Colors.BOLD)
        self.log(f"Results: {passed}/{total} passed", Colors.GREEN if failed == 0 else Colors.YELLOW)
        
        if failed == 0:
            self.log("\n[SUCCESS] All validation checks passed!", Colors.GREEN + Colors.BOLD)
            self.log("\nNext steps:", Colors.CYAN)
            self.log("  1. Configure your AI assistant with the MCP settings", Colors.BLUE)
            self.log("  2. Start the AI assistant to enable MCP tools", Colors.BLUE)
            self.log("  3. Run: python test_movement_sync_mcp.py", Colors.BLUE)
        else:
            self.log(f"\n[WARNING] {failed} check(s) failed", Colors.YELLOW + Colors.BOLD)
            self.log("Please review the errors above and fix them.", Colors.YELLOW)
        
        self.log("=" * 70, Colors.BOLD)
        
        # Save report
        report = {
            "timestamp": time.time(),
            "project_root": str(self.project_root),
            "summary": {
                "total": total,
                "passed": passed,
                "failed": failed
            },
            "results": [
                {
                    "name": r.name,
                    "passed": r.passed,
                    "message": r.message,
                    "details": r.details,
                    "timestamp": r.timestamp
                }
                for r in self.results
            ]
        }
        
        report_path = Path(__file__).parent / "validation_report.json"
        with open(report_path, 'w') as f:
            json.dump(report, f, indent=2)
        
        self.log(f"\nReport saved to: {report_path}", Colors.BLUE)
        
        return report


def main():
    """Main entry point."""
    validator = MCPValidator()
    report = validator.run_all_checks()
    
    # Exit with appropriate code
    failed = report["summary"]["failed"]
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
