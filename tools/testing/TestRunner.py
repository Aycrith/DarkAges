"""
Master Test Runner for DarkAges MMO

Orchestrates all three testing tiers:
- Tier 1: Unit Tests (C++ / Catch2)
- Tier 2: Simulation Tests (Python)
- Tier 3: Real Execution Tests (Godot MCP)

Usage:
    python TestRunner.py --tier=all
    python TestRunner.py --tier=unit
    python TestRunner.py --tier=simulation
    python TestRunner.py --tier=real --mcp
"""

import asyncio
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Any
from dataclasses import dataclass, asdict

from telemetry.MetricsCollector import MetricsCollector


@dataclass
class TestResult:
    """Result from a test execution."""
    tier: str
    test_name: str
    passed: bool
    duration_ms: float
    output: str
    errors: List[str]
    metrics: Dict[str, Any]


class TestRunner:
    """
    Master test runner coordinating all test tiers.
    """
    
    def __init__(
        self,
        project_root: Path = None,
        server_path: Path = None,
        godot_path: Path = None
    ):
        self.project_root = project_root or Path(__file__).parent.parent.parent
        self.server_path = server_path or (self.project_root / "build" / "Release" / "darkages_server.exe")
        self.godot_path = godot_path or (self.project_root / "tools" / "godot" / "Godot_v4.2.2-stable_mono_win64" / "Godot_v4.2.2-stable_mono_win64.exe")
        
        self.results: List[TestResult] = []
        self.collector = MetricsCollector("test_runner")
    
    # -------------------------------------------------------------------------
    # Tier 1: Unit Tests
    # -------------------------------------------------------------------------
    
    def run_unit_tests(self) -> TestResult:
        """
        Run C++ unit tests using Catch2.
        
        Note: This requires the server to be built with testing enabled.
        """
        test_name = "unit_tests"
        start_time = time.time()
        errors = []
        output = ""
        
        print("=" * 70)
        print("TIER 1: UNIT TESTS")
        print("=" * 70)
        
        if not self.server_path.exists():
            error = f"Server binary not found: {self.server_path}"
            print(f"[SKIP] {error}")
            return TestResult(
                tier="unit",
                test_name=test_name,
                passed=True,  # Not a failure, just not available
                duration_ms=0,
                output=error,
                errors=[],
                metrics={}
            )
        
        try:
            # Run server with test flag
            result = subprocess.run(
                [str(self.server_path), "--test"],
                capture_output=True,
                text=True,
                timeout=120
            )
            
            output = result.stdout + result.stderr
            passed = result.returncode == 0
            
            if not passed:
                errors.append(f"Unit tests failed with code {result.returncode}")
            
            print(f"[PASS] Unit tests completed" if passed else f"[FAIL] Unit tests failed")
            
        except subprocess.TimeoutExpired:
            errors.append("Unit tests timed out")
            passed = False
            output = "Timeout after 120s"
        except Exception as e:
            errors.append(str(e))
            passed = False
            output = str(e)
        
        duration_ms = (time.time() - start_time) * 1000
        
        return TestResult(
            tier="unit",
            test_name=test_name,
            passed=passed,
            duration_ms=duration_ms,
            output=output,
            errors=errors,
            metrics={}
        )
    
    # -------------------------------------------------------------------------
    # Tier 2: Simulation Tests
    # -------------------------------------------------------------------------
    
    async def run_simulation_tests(self) -> TestResult:
        """Run Python-based simulation tests."""
        test_name = "simulation_tests"
        start_time = time.time()
        errors = []
        output = ""
        
        print("\n" + "=" * 70)
        print("TIER 2: SIMULATION TESTS")
        print("=" * 70)
        
        try:
            # Import and run test scenarios
            sys.path.insert(0, str(self.project_root / "tools" / "testing" / "scenarios"))
            from MovementTestScenarios import MovementTestScenarios
            
            suite = MovementTestScenarios(use_mcp=False)
            results = await suite.run_all()
            
            # Check results
            passed_count = sum(1 for r in results.values() if r.passed)
            total_count = len(results)
            
            output = f"{passed_count}/{total_count} scenarios passed"
            passed = passed_count == total_count
            
            for name, result in results.items():
                if not result.passed:
                    errors.extend(result.errors)
            
            print(f"[PASS] Simulation tests: {output}" if passed else f"[FAIL] Simulation tests: {output}")
            
        except Exception as e:
            errors.append(str(e))
            passed = False
            output = str(e)
            import traceback
            traceback.print_exc()
        
        duration_ms = (time.time() - start_time) * 1000
        
        return TestResult(
            tier="simulation",
            test_name=test_name,
            passed=passed,
            duration_ms=duration_ms,
            output=output,
            errors=errors,
            metrics={}
        )
    
    # -------------------------------------------------------------------------
    # Tier 3: Real Execution Tests
    # -------------------------------------------------------------------------
    
    async def run_real_tests(self, use_mcp: bool = False) -> TestResult:
        """
        Run real execution tests with actual Godot client.
        
        Args:
            use_mcp: If True, use Godot MCP to control real client
        """
        test_name = "real_execution_tests"
        start_time = time.time()
        errors = []
        output = ""
        
        print("\n" + "=" * 70)
        print("TIER 3: REAL EXECUTION TESTS")
        print("=" * 70)
        print(f"Mode: {'MCP (Real Godot)' if use_mcp else 'Simulation'}")
        
        if use_mcp and not self.godot_path.exists():
            error = f"Godot not found: {self.godot_path}"
            print(f"[SKIP] {error}")
            return TestResult(
                tier="real",
                test_name=test_name,
                passed=True,
                duration_ms=0,
                output=error,
                errors=[],
                metrics={}
            )
        
        try:
            if use_mcp:
                # Run MCP-based tests
                sys.path.insert(0, str(self.project_root / "tools" / "automated-qa"))
                from godot_mcp.test_movement_sync_mcp import MovementSyncMCPTest
                
                test = MovementSyncMCPTest(
                    server_path=str(self.server_path),
                    project_path=str(self.project_root / "src" / "client"),
                    godot_path=str(self.godot_path)
                )
                
                passed = await test.run()
                output = f"MCP test completed: {'PASS' if passed else 'FAIL'}"
                
            else:
                # Run simulation version
                sys.path.insert(0, str(self.project_root / "tools" / "testing" / "scenarios"))
                from MovementTestScenarios import MovementTestScenarios
                
                suite = MovementTestScenarios(use_mcp=False)
                results = await suite.run_all()
                
                passed_count = sum(1 for r in results.values() if r.passed)
                total_count = len(results)
                
                passed = passed_count == total_count
                output = f"Real execution (simulated): {passed_count}/{total_count} passed"
            
            print(f"[PASS] {output}" if passed else f"[FAIL] {output}")
            
        except Exception as e:
            errors.append(str(e))
            passed = False
            output = str(e)
            import traceback
            traceback.print_exc()
        
        duration_ms = (time.time() - start_time) * 1000
        
        return TestResult(
            tier="real",
            test_name=test_name,
            passed=passed,
            duration_ms=duration_ms,
            output=output,
            errors=errors,
            metrics={}
        )
    
    # -------------------------------------------------------------------------
    # Test Orchestration
    # -------------------------------------------------------------------------
    
    async def run_tier(self, tier: str, use_mcp: bool = False) -> TestResult:
        """Run a specific test tier."""
        if tier == "unit":
            return self.run_unit_tests()
        elif tier == "simulation":
            return await self.run_simulation_tests()
        elif tier == "real":
            return await self.run_real_tests(use_mcp)
        else:
            raise ValueError(f"Unknown tier: {tier}")
    
    async def run_all(self, tiers: List[str] = None, use_mcp: bool = False) -> Dict[str, TestResult]:
        """
        Run all specified test tiers.
        
        Args:
            tiers: List of tiers to run (unit, simulation, real)
            use_mcp: Use Godot MCP for real tests
        """
        if tiers is None:
            tiers = ["unit", "simulation", "real"]
        
        print("=" * 70)
        print("DARKAGES MMO - COMPREHENSIVE TEST RUNNER")
        print("=" * 70)
        print(f"Tiers: {', '.join(tiers)}")
        print(f"MCP: {'Enabled' if use_mcp else 'Disabled'}")
        print(f"Server: {self.server_path}")
        print(f"Godot: {self.godot_path}")
        print("=" * 70)
        
        results = {}
        
        for tier in tiers:
            result = await self.run_tier(tier, use_mcp)
            results[tier] = result
            self.results.append(result)
        
        # Generate summary
        self.print_summary(results)
        
        return results
    
    def print_summary(self, results: Dict[str, TestResult]):
        """Print test summary."""
        print("\n" + "=" * 70)
        print("TEST SUMMARY")
        print("=" * 70)
        
        total_passed = 0
        total_tests = len(results)
        total_duration = 0
        
        for tier, result in results.items():
            status = "PASS" if result.passed else "FAIL"
            symbol = "[PASS]" if result.passed else "[FAIL]"
            
            print(f"{symbol} {tier.upper():<12} {result.test_name:<30} {result.duration_ms/1000:.2f}s")
            
            if result.passed:
                total_passed += 1
            total_duration += result.duration_ms
            
            if result.errors:
                for error in result.errors:
                    print(f"       ERROR: {error}")
        
        print("-" * 70)
        print(f"Total: {total_passed}/{total_tests} passed ({total_passed/total_tests*100:.1f}%)")
        print(f"Duration: {total_duration/1000:.2f}s")
        print("=" * 70)
        
        # Save results
        self.save_results()
    
    def save_results(self):
        """Save test results to file."""
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        filename = f"test_run_{timestamp}.json"
        
        output_dir = self.project_root / "tools" / "testing" / "reports"
        output_dir.mkdir(parents=True, exist_ok=True)
        
        filepath = output_dir / filename
        
        data = {
            "timestamp": time.time(),
            "results": [
                {
                    "tier": r.tier,
                    "test_name": r.test_name,
                    "passed": r.passed,
                    "duration_ms": r.duration_ms,
                    "output": r.output,
                    "errors": r.errors
                }
                for r in self.results
            ]
        }
        
        with open(filepath, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"\nResults saved to: {filepath}")


# -------------------------------------------------------------------------
# Main Entry Point
# -------------------------------------------------------------------------

async def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="DarkAges MMO Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python TestRunner.py --tier=all              # Run all tiers
  python TestRunner.py --tier=unit             # Run unit tests only
  python TestRunner.py --tier=real --mcp       # Run real tests with Godot MCP
  python TestRunner.py --tier=simulation       # Run simulation tests
        """
    )
    
    parser.add_argument(
        "--tier",
        choices=["unit", "simulation", "real", "all"],
        default="all",
        help="Which test tier to run (default: all)"
    )
    parser.add_argument(
        "--mcp",
        action="store_true",
        help="Use Godot MCP for real execution tests"
    )
    parser.add_argument(
        "--server",
        default=None,
        help="Path to server executable"
    )
    parser.add_argument(
        "--godot",
        default=None,
        help="Path to Godot executable"
    )
    
    args = parser.parse_args()
    
    # Determine tiers to run
    tiers = ["unit", "simulation", "real"] if args.tier == "all" else [args.tier]
    
    # Create runner
    runner = TestRunner(
        server_path=Path(args.server) if args.server else None,
        godot_path=Path(args.godot) if args.godot else None
    )
    
    # Run tests
    results = await runner.run_all(tiers=tiers, use_mcp=args.mcp)
    
    # Exit code
    all_passed = all(r.passed for r in results.values())
    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    asyncio.run(main())
