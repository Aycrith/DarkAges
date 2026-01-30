#!/usr/bin/env python3
"""
End-to-End Integration Test for DarkAges MMO
Automated test runner for full system validation.

Usage:
    python e2e_test.py --full       # Run full test suite
    python e2e_test.py --quick      # Quick smoke test only
    python e2e_test.py --build      # Build before testing
    python e2e_test.py --report     # Generate HTML report

Exit Codes:
    0 - All tests passed
    1 - One or more tests failed
    2 - Critical failure (build failed)
"""

import subprocess
import sys
import time
import json
import argparse
import os
from datetime import datetime
from pathlib import Path

# Test configuration
PROJECT_ROOT = Path(__file__).parent.parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
SERVER_EXE = BUILD_DIR / "Release" / "darkages_server.exe"
INFRA_DIR = PROJECT_ROOT / "infra"
TEST_DIR = Path(__file__).parent

# Test results storage
results = {}


def run_command(name: str, command: list, cwd: Path = None, timeout: int = 60) -> tuple:
    """Run a command and return success status and output."""
    print(f"\n[TEST] {name}")
    print("-" * 60)
    
    try:
        result = subprocess.run(
            command,
            cwd=cwd or PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        
        success = result.returncode == 0
        output = result.stdout + result.stderr
        
        status = "PASS" if success else "FAIL"
        print(f"Status: {status}")
        
        if not success and output:
            # Show last 500 chars of output on failure
            print(f"Output: ...{output[-500:]}")
        
        return success, output
        
    except subprocess.TimeoutExpired:
        print(f"Status: TIMEOUT")
        return False, "Test timed out"
    except Exception as e:
        print(f"Status: ERROR - {e}")
        return False, str(e)


def test_prerequisites() -> bool:
    """Check that required tools are available."""
    print("=" * 60)
    print("Phase 0: Prerequisites Check")
    print("=" * 60)
    
    checks = [
        ("CMake", ["cmake", "--version"]),
        ("Python", [sys.executable, "--version"]),
        ("Redis", ["redis-cli", "--version"]),
    ]
    
    all_passed = True
    for name, cmd in checks:
        try:
            subprocess.run(cmd, capture_output=True, check=True)
            print(f"  {name}: OK")
        except (subprocess.CalledProcessError, FileNotFoundError):
            print(f"  {name}: NOT FOUND")
            all_passed = False
    
    return all_passed


def test_build() -> bool:
    """Build the server."""
    print("\n" + "=" * 60)
    print("Phase 1: Server Build")
    print("=" * 60)
    
    # Create build directory
    BUILD_DIR.mkdir(exist_ok=True)
    
    # Configure
    success, output = run_command(
        "CMake Configure",
        [
            "cmake", "..",
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_GNS=OFF",
            "-DENABLE_REDIS=OFF",
            "-DENABLE_SCYLLA=OFF",
            "-DENABLE_FLATBUFFERS=OFF"
        ],
        cwd=BUILD_DIR,
        timeout=120
    )
    
    if not success:
        return False
    
    # Build
    success, output = run_command(
        "CMake Build",
        ["cmake", "--build", ".", "--config", "Release"],
        cwd=BUILD_DIR,
        timeout=300
    )
    
    return success


def test_server_startup() -> bool:
    """Test that the server starts successfully."""
    print("\n" + "=" * 60)
    print("Phase 2: Server Startup")
    print("=" * 60)
    
    if not SERVER_EXE.exists():
        print(f"Server executable not found: {SERVER_EXE}")
        return False
    
    # Run server for 5 seconds to verify it starts
    try:
        proc = subprocess.Popen(
            [str(SERVER_EXE)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Wait for startup
        time.sleep(3)
        
        # Check output for success message (server may exit after self-test)
        stdout, stderr = proc.communicate()
        output = stdout + stderr
        
        if "Basic verification passed" in output or proc.poll() == 0:
            print("Server started and passed self-tests")
            return True
        else:
            print(f"Server failed:\n{output}")
            return False
            
    except Exception as e:
        print(f"Failed to start server: {e}")
        return False


def test_unit_tests() -> bool:
    """Run C++ unit tests."""
    print("\n" + "=" * 60)
    print("Phase 3: Unit Tests")
    print("=" * 60)
    
    test_exe = BUILD_DIR / "Release" / "darkages_tests.exe"
    if not test_exe.exists():
        print("Test executable not found (compilation may have failed)")
        return False
    
    success, output = run_command(
        "C++ Unit Tests",
        [str(test_exe)],
        timeout=120
    )
    
    return success


def test_infrastructure() -> bool:
    """Test Redis and ScyllaDB connectivity."""
    print("\n" + "=" * 60)
    print("Phase 4: Infrastructure")
    print("=" * 60)
    
    # Test Redis
    redis_ok = False
    try:
        import redis as redis_lib
        client = redis_lib.Redis(
            host='localhost',
            port=6379,
            socket_connect_timeout=2,
            socket_timeout=2
        )
        client.ping()
        print("  Redis: PASS")
        redis_ok = True
    except Exception as e:
        print(f"  Redis: FAIL ({e})")
    
    # Test ScyllaDB (optional)
    scylla_ok = False
    try:
        from cassandra.cluster import Cluster
        cluster = Cluster(['localhost'], port=9042)
        session = cluster.connect()
        session.execute("SELECT now() FROM system.local")
        cluster.shutdown()
        print("  ScyllaDB: PASS")
        scylla_ok = True
    except Exception as e:
        print(f"  ScyllaDB: SKIP ({e})")
    
    # Redis is required, ScyllaDB is optional for basic tests
    return redis_ok


def test_python_integration() -> bool:
    """Run Python integration harness."""
    print("\n" + "=" * 60)
    print("Phase 5: Python Integration Tests")
    print("=" * 60)
    
    # Install dependencies first
    run_command(
        "Install Python Dependencies",
        [sys.executable, "-m", "pip", "install", "-r", "requirements.txt"],
        cwd=TEST_DIR,
        timeout=60
    )
    
    # Run health check
    success, output = run_command(
        "Service Health Check",
        [sys.executable, "integration_harness.py", "--health"],
        cwd=TEST_DIR,
        timeout=30
    )
    
    return success


def test_bot_connectivity() -> bool:
    """Test bot connectivity (requires full server)."""
    print("\n" + "=" * 60)
    print("Phase 6: Bot Connectivity")
    print("=" * 60)
    
    success, output = run_command(
        "Basic Connectivity Test",
        [sys.executable, "integration_harness.py", "--test", "basic_connectivity"],
        cwd=TEST_DIR,
        timeout=30
    )
    
    return success


def test_stress() -> bool:
    """Run stress test with multiple bots."""
    print("\n" + "=" * 60)
    print("Phase 7: Stress Test")
    print("=" * 60)
    
    success, output = run_command(
        "50-Player Stress Test (60s)",
        [sys.executable, "integration_harness.py", "--stress", "50", "--duration", "60"],
        cwd=TEST_DIR,
        timeout=120
    )
    
    return success


def generate_report(output_path: str = None):
    """Generate test report."""
    print("\n" + "=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    
    passed = sum(1 for success, _ in results.values() if success)
    total = len(results)
    
    for name, (success, _) in results.items():
        status = "PASS" if success else "FAIL"
        print(f"  {status:4} {name}")
    
    print("-" * 60)
    print(f"Total: {passed}/{total} tests passed")
    
    # Save JSON report
    report = {
        "timestamp": datetime.now().isoformat(),
        "results": {name: {"passed": success, "output": output} 
                   for name, (success, output) in results.items()},
        "summary": {"passed": passed, "total": total}
    }
    
    report_path = output_path or "E2E_TEST_REPORT.json"
    with open(report_path, "w") as f:
        json.dump(report, f, indent=2)
    
    print(f"\nReport saved to: {report_path}")
    
    return passed == total


def main():
    parser = argparse.ArgumentParser(
        description="DarkAges MMO End-to-End Integration Test"
    )
    parser.add_argument("--full", action="store_true", 
                       help="Run full test suite (includes stress test)")
    parser.add_argument("--quick", action="store_true",
                       help="Quick smoke test only")
    parser.add_argument("--build", action="store_true",
                       help="Build server before testing")
    parser.add_argument("--report", type=str,
                       help="Save report to specified path")
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("DarkAges MMO End-to-End Integration Test")
    print(f"Started: {datetime.now()}")
    print("=" * 60)
    
    # Phase 0: Prerequisites
    if not test_prerequisites():
        print("\n[ERROR] Prerequisites not met")
        return 2
    
    # Phase 1: Build (if requested)
    if args.build or args.full:
        results["build"] = (test_build(), "")
        if not results["build"][0]:
            print("\n[CRITICAL] Build failed")
            return 2
    
    # Quick mode - just server startup
    if args.quick:
        results["server_startup"] = (test_server_startup(), "")
        generate_report(args.report)
        return 0 if results["server_startup"][0] else 1
    
    # Standard tests
    results["server_startup"] = (test_server_startup(), "")
    results["infrastructure"] = (test_infrastructure(), "")
    results["python_integration"] = (test_python_integration(), "")
    
    # Full mode - additional tests
    if args.full:
        results["unit_tests"] = (test_unit_tests(), "")
        results["bot_connectivity"] = (test_bot_connectivity(), "")
        results["stress_test"] = (test_stress(), "")
    
    # Generate report
    all_passed = generate_report(args.report)
    
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
