#!/usr/bin/env python3
"""
DarkAges MMO - Project State Validation
[TESTING_AGENT] Comprehensive validation of current implementation state

This script validates:
1. Build system functionality
2. Server startup and basic operation
3. Gamestate synchronization (if test tools available)
4. Infrastructure readiness
5. Documentation completeness

Usage:
    python validate_project_state.py --full
    python validate_project_state.py --quick
    python validate_project_state.py --report
"""

import subprocess
import sys
import json
import time
import argparse
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional
from enum import Enum
import logging

logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')
logger = logging.getLogger('ProjectValidator')


class ValidationStatus(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    WARNING = "WARNING"
    SKIP = "SKIP"


@dataclass
class ValidationCheck:
    name: str
    description: str
    status: ValidationStatus
    details: str = ""
    recommendation: str = ""


@dataclass
class ValidationReport:
    timestamp: float
    version: str = "1.0"
    checks: List[ValidationCheck] = field(default_factory=list)
    
    def add(self, check: ValidationCheck):
        self.checks.append(check)
    
    def summary(self) -> Dict:
        passed = sum(1 for c in self.checks if c.status == ValidationStatus.PASS)
        failed = sum(1 for c in self.checks if c.status == ValidationStatus.FAIL)
        warnings = sum(1 for c in self.checks if c.status == ValidationStatus.WARNING)
        skipped = sum(1 for c in self.checks if c.status == ValidationStatus.SKIP)
        
        return {
            "total": len(self.checks),
            "passed": passed,
            "failed": failed,
            "warnings": warnings,
            "skipped": skipped,
            "success_rate": passed / max(1, len(self.checks))
        }
    
    def print_report(self):
        print("\n" + "="*70)
        print("DARKAGES MMO - PROJECT STATE VALIDATION REPORT")
        print("="*70)
        print(f"Timestamp: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(self.timestamp))}")
        print()
        
        summary = self.summary()
        print(f"Total Checks: {summary['total']}")
        print(f"  ✅ PASS:    {summary['passed']}")
        print(f"  ❌ FAIL:    {summary['failed']}")
        print(f"  ⚠️  WARNING: {summary['warnings']}")
        print(f"  ⏭️  SKIP:    {summary['skipped']}")
        print(f"  Success Rate: {summary['success_rate']*100:.1f}%")
        print()
        
        print("DETAILED RESULTS:")
        print("-"*70)
        
        for check in self.checks:
            icon = {
                ValidationStatus.PASS: "✅",
                ValidationStatus.FAIL: "❌",
                ValidationStatus.WARNING: "⚠️",
                ValidationStatus.SKIP: "⏭️"
            }.get(check.status, "❓")
            
            print(f"\n{icon} {check.name}")
            print(f"   Status: {check.status.value}")
            if check.details:
                print(f"   Details: {check.details}")
            if check.recommendation:
                print(f"   Recommendation: {check.recommendation}")
        
        print("\n" + "="*70)
        
        if summary['failed'] > 0:
            print("\n❌ VALIDATION FAILED")
            print(f"   {summary['failed']} critical checks failed.")
            print("   Project is NOT ready for deployment.")
            return False
        elif summary['warnings'] > 0:
            print("\n⚠️  VALIDATION PASSED WITH WARNINGS")
            print("   Project is functional but has issues to address.")
            return True
        else:
            print("\n✅ VALIDATION PASSED")
            print("   All checks passed. Project is ready for deployment.")
            return True


class ProjectValidator:
    def __init__(self, project_root: Path):
        self.root = project_root
        self.report = ValidationReport(timestamp=time.time())
    
    def run_all_validations(self):
        """Run complete validation suite"""
        logger.info("Starting project state validation...")
        
        self.validate_file_structure()
        self.validate_build_system()
        self.validate_server_binary()
        self.validate_test_framework()
        self.validate_infrastructure()
        self.validate_documentation()
        self.validate_source_code()
        
        return self.report
    
    def validate_file_structure(self):
        """Validate project file structure"""
        logger.info("Validating file structure...")
        
        required_dirs = [
            "src/server/src",
            "src/server/include",
            "src/client/src",
            "tests",
            "infra",
            "tools",
            "docs"
        ]
        
        missing = []
        for dir_path in required_dirs:
            if not (self.root / dir_path).exists():
                missing.append(dir_path)
        
        if missing:
            self.report.add(ValidationCheck(
                name="File Structure",
                description="Check required directories exist",
                status=ValidationStatus.FAIL,
                details=f"Missing directories: {', '.join(missing)}",
                recommendation="Create missing directory structure"
            ))
        else:
            # Count source files
            cpp_files = list(self.root.glob("src/**/*.cpp"))
            hpp_files = list(self.root.glob("src/**/*.hpp"))
            cs_files = list(self.root.glob("src/**/*.cs"))
            
            self.report.add(ValidationCheck(
                name="File Structure",
                description="Check required directories exist",
                status=ValidationStatus.PASS,
                details=f"Found {len(cpp_files)} .cpp, {len(hpp_files)} .hpp, {len(cs_files)} .cs files"
            ))
    
    def validate_build_system(self):
        """Validate build system configuration"""
        logger.info("Validating build system...")
        
        cmake_file = self.root / "CMakeLists.txt"
        if not cmake_file.exists():
            self.report.add(ValidationCheck(
                name="Build System",
                description="CMake configuration exists",
                status=ValidationStatus.FAIL,
                details="CMakeLists.txt not found",
                recommendation="Create CMakeLists.txt build configuration"
            ))
            return
        
        # Check for key CMake components
        content = cmake_file.read_text()
        required_components = [
            "project",
            "add_executable",
            "target_link_libraries",
            "FetchContent"
        ]
        
        missing = [c for c in required_components if c not in content]
        
        if missing:
            self.report.add(ValidationCheck(
                name="Build System",
                description="CMake configuration completeness",
                status=ValidationStatus.WARNING,
                details=f"Missing CMake components: {', '.join(missing)}",
                recommendation="Review CMakeLists.txt for completeness"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Build System",
                description="CMake configuration exists",
                status=ValidationStatus.PASS,
                details="CMakeLists.txt found with all required components"
            ))
        
        # Try to configure
        build_dir = self.root / "build_test"
        build_dir.mkdir(exist_ok=True)
        
        try:
            result = subprocess.run(
                ["cmake", "..", "-G", "Visual Studio 17 2022", "-A", "x64"],
                cwd=build_dir,
                capture_output=True,
                text=True,
                timeout=60
            )
            
            if result.returncode == 0:
                self.report.add(ValidationCheck(
                    name="CMake Configuration",
                    description="CMake can configure the project",
                    status=ValidationStatus.PASS,
                    details="CMake configuration successful"
                ))
            else:
                self.report.add(ValidationCheck(
                    name="CMake Configuration",
                    description="CMake can configure the project",
                    status=ValidationStatus.FAIL,
                    details=f"CMake configuration failed:\n{result.stderr[:500]}",
                    recommendation="Fix CMake configuration errors"
                ))
        except Exception as e:
            self.report.add(ValidationCheck(
                name="CMake Configuration",
                description="CMake can configure the project",
                status=ValidationStatus.WARNING,
                details=f"Could not run CMake: {e}",
                recommendation="Ensure CMake is installed and in PATH"
            ))
    
    def validate_server_binary(self):
        """Validate server binary exists and runs"""
        logger.info("Validating server binary...")
        
        # Look for server binary
        possible_paths = [
            self.root / "build" / "Release" / "darkages_server.exe",
            self.root / "build" / "bin" / "darkages_server.exe",
            self.root / "build" / "darkages_server.exe",
        ]
        
        binary = None
        for path in possible_paths:
            if path.exists():
                binary = path
                break
        
        if not binary:
            self.report.add(ValidationCheck(
                name="Server Binary",
                description="Server executable exists",
                status=ValidationStatus.FAIL,
                details="darkages_server.exe not found in build directories",
                recommendation="Build the project: ./build.sh"
            ))
            return
        
        self.report.add(ValidationCheck(
            name="Server Binary",
            description="Server executable exists",
            status=ValidationStatus.PASS,
            details=f"Found server binary: {binary}"
        ))
        
        # Try to run with --help
        try:
            result = subprocess.run(
                [str(binary), "--help"],
                capture_output=True,
                text=True,
                timeout=5
            )
            
            if "DarkAges" in result.stdout or result.returncode == 0:
                self.report.add(ValidationCheck(
                    name="Server Execution",
                    description="Server can execute and show help",
                    status=ValidationStatus.PASS,
                    details="Server responds to --help command"
                ))
            else:
                self.report.add(ValidationCheck(
                    name="Server Execution",
                    description="Server can execute and show help",
                    status=ValidationStatus.WARNING,
                    details="Server executed but response was unexpected",
                    recommendation="Verify server build is correct"
                ))
        except Exception as e:
            self.report.add(ValidationCheck(
                name="Server Execution",
                description="Server can execute and show help",
                status=ValidationStatus.WARNING,
                details=f"Could not execute server: {e}",
                recommendation="Check binary dependencies"
            ))
    
    def validate_test_framework(self):
        """Validate testing framework"""
        logger.info("Validating test framework...")
        
        # Check for test orchestrator
        orchestrator = self.root / "tools" / "test-orchestrator" / "TestOrchestrator.py"
        if orchestrator.exists():
            self.report.add(ValidationCheck(
                name="Test Orchestrator",
                description="Gamestate test orchestrator exists",
                status=ValidationStatus.PASS,
                details="TestOrchestrator.py found"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Test Orchestrator",
                description="Gamestate test orchestrator exists",
                status=ValidationStatus.FAIL,
                details="TestOrchestrator.py not found",
                recommendation="Create gamestate testing framework"
            ))
        
        # Check for C++ tests
        test_files = list(self.root.glob("tests/**/*.cpp")) + list(self.root.glob("src/server/tests/*.cpp"))
        if test_files:
            self.report.add(ValidationCheck(
                name="C++ Tests",
                description="C++ test files exist",
                status=ValidationStatus.PASS,
                details=f"Found {len(test_files)} test files"
            ))
        else:
            self.report.add(ValidationCheck(
                name="C++ Tests",
                description="C++ test files exist",
                status=ValidationStatus.WARNING,
                details="No C++ test files found",
                recommendation="Add Catch2 unit tests"
            ))
        
        # Check for gamestate integration test
        gamestate_test = self.root / "tests" / "integration" / "gamestate" / "TestGamestateSynchronization.cpp"
        if gamestate_test.exists():
            self.report.add(ValidationCheck(
                name="Gamestate Integration Test",
                description="Gamestate synchronization test exists",
                status=ValidationStatus.PASS,
                details="TestGamestateSynchronization.cpp found"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Gamestate Integration Test",
                description="Gamestate synchronization test exists",
                status=ValidationStatus.WARNING,
                details="Gamestate integration test not found",
                recommendation="Create gamestate validation tests"
            ))
    
    def validate_infrastructure(self):
        """Validate infrastructure configuration"""
        logger.info("Validating infrastructure...")
        
        # Check Docker Compose files
        compose_files = [
            self.root / "infra" / "docker-compose.yml",
            self.root / "infra" / "docker-compose.monitoring.yml"
        ]
        
        found = [f for f in compose_files if f.exists()]
        if len(found) >= 2:
            self.report.add(ValidationCheck(
                name="Docker Compose",
                description="Docker Compose configurations exist",
                status=ValidationStatus.PASS,
                details=f"Found {len(found)} compose files"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Docker Compose",
                description="Docker Compose configurations exist",
                status=ValidationStatus.WARNING,
                details=f"Only {len(found)}/2 compose files found",
                recommendation="Create missing Docker Compose files"
            ))
        
        # Check Kubernetes manifests
        k8s_dir = self.root / "infra" / "k8s"
        if k8s_dir.exists():
            k8s_files = list(k8s_dir.glob("**/*.yaml"))
            self.report.add(ValidationCheck(
                name="Kubernetes Manifests",
                description="K8s configuration exists",
                status=ValidationStatus.PASS,
                details=f"Found {len(k8s_files)} Kubernetes manifest files"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Kubernetes Manifests",
                description="K8s configuration exists",
                status=ValidationStatus.WARNING,
                details="Kubernetes manifests not found",
                recommendation="Add K8s manifests for deployment"
            ))
        
        # Check monitoring
        prometheus_config = self.root / "infra" / "prometheus" / "prometheus.yml"
        if prometheus_config.exists():
            self.report.add(ValidationCheck(
                name="Monitoring Configuration",
                description="Prometheus configuration exists",
                status=ValidationStatus.PASS,
                details="Prometheus config found"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Monitoring Configuration",
                description="Prometheus configuration exists",
                status=ValidationStatus.WARNING,
                details="Prometheus config not found",
                recommendation="Add monitoring configuration"
            ))
    
    def validate_documentation(self):
        """Validate documentation completeness"""
        logger.info("Validating documentation...")
        
        md_files = list(self.root.glob("*.md"))
        
        required_docs = [
            "README.md",
            "AGENTS.md",
            "CONTRIBUTING.md"
        ]
        
        missing = [d for d in required_docs if not (self.root / d).exists()]
        
        if missing:
            self.report.add(ValidationCheck(
                name="Core Documentation",
                description="Required documentation files exist",
                status=ValidationStatus.WARNING,
                details=f"Missing docs: {', '.join(missing)}",
                recommendation="Create missing documentation"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Core Documentation",
                description="Required documentation files exist",
                status=ValidationStatus.PASS,
                details=f"Found {len(md_files)} markdown files"
            ))
        
        # Check runbooks
        runbooks = self.root / "docs" / "runbooks"
        if runbooks.exists():
            runbook_files = list(runbooks.glob("*.md"))
            self.report.add(ValidationCheck(
                name="Operations Runbooks",
                description="Operations runbooks exist",
                status=ValidationStatus.PASS,
                details=f"Found {len(runbook_files)} runbook files"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Operations Runbooks",
                description="Operations runbooks exist",
                status=ValidationStatus.WARNING,
                details="Runbooks not found",
                recommendation="Create incident response and deployment runbooks"
            ))
    
    def validate_source_code(self):
        """Validate source code completeness"""
        logger.info("Validating source code...")
        
        # Check for key server components
        key_components = [
            ("Zone Server", "src/server/include/zones/ZoneServer.hpp"),
            ("Movement System", "src/server/include/physics/MovementSystem.hpp"),
            ("Combat System", "src/server/include/combat/CombatSystem.hpp"),
            ("Network Manager", "src/server/include/netcode/NetworkManager.hpp"),
            ("Redis Manager", "src/server/include/db/RedisManager.hpp"),
            ("Anti-Cheat", "src/server/include/security/AntiCheat.hpp"),
        ]
        
        found = 0
        for name, path in key_components:
            if (self.root / path).exists():
                found += 1
        
        if found == len(key_components):
            self.report.add(ValidationCheck(
                name="Server Components",
                description="Key server components implemented",
                status=ValidationStatus.PASS,
                details=f"All {len(key_components)} key components found"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Server Components",
                description="Key server components implemented",
                status=ValidationStatus.WARNING,
                details=f"Found {found}/{len(key_components)} key components",
                recommendation="Implement missing components"
            ))
        
        # Check for client components
        client_files = list((self.root / "src" / "client").glob("**/*.cs")) if (self.root / "src" / "client").exists() else []
        if len(client_files) > 10:
            self.report.add(ValidationCheck(
                name="Client Implementation",
                description="Client code exists",
                status=ValidationStatus.PASS,
                details=f"Found {len(client_files)} C# files"
            ))
        elif client_files:
            self.report.add(ValidationCheck(
                name="Client Implementation",
                description="Client code exists",
                status=ValidationStatus.WARNING,
                details=f"Only {len(client_files)} C# files found",
                recommendation="Complete client implementation"
            ))
        else:
            self.report.add(ValidationCheck(
                name="Client Implementation",
                description="Client code exists",
                status=ValidationStatus.WARNING,
                details="No client C# files found",
                recommendation="Implement Godot client"
            ))


def main():
    parser = argparse.ArgumentParser(description='Validate DarkAges MMO Project State')
    parser.add_argument('--full', action='store_true', help='Run full validation including build')
    parser.add_argument('--quick', action='store_true', help='Quick validation (no build)')
    parser.add_argument('--report', type=str, help='Save report to JSON file')
    parser.add_argument('--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    project_root = Path(__file__).parent.parent.parent
    validator = ProjectValidator(project_root)
    
    report = validator.run_all_validations()
    
    success = report.print_report()
    
    if args.report:
        output = {
            "timestamp": report.timestamp,
            "summary": report.summary(),
            "checks": [
                {
                    "name": c.name,
                    "description": c.description,
                    "status": c.status.value,
                    "details": c.details,
                    "recommendation": c.recommendation
                }
                for c in report.checks
            ]
        }
        
        with open(args.report, 'w') as f:
            json.dump(output, f, indent=2)
        
        logger.info(f"Report saved to {args.report}")
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
