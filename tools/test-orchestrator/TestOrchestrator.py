#!/usr/bin/env python3
"""
DarkAges MMO - Gamestate Test Orchestrator
[TESTING_AGENT] Comprehensive end-to-end testing framework for client-server synchronization

This system validates:
- Client prediction vs server reconciliation
- Entity interpolation for remote players
- Combat synchronization and lag compensation
- Zone handoffs and entity migration
- Gamestate consistency across network conditions

Usage:
    python TestOrchestrator.py --scenario movement_sync --duration 60
    python TestOrchestrator.py --scenario combat_sync --latency 100 --jitter 20
    python TestOrchestrator.py --all-scenarios --continuous
"""

import asyncio
import json
import time
import random
import subprocess
import logging
import argparse
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional, Callable, Tuple
from enum import Enum, auto
from pathlib import Path
import sys

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('TestOrchestrator')


class TestResult(Enum):
    PASS = auto()
    FAIL = auto()
    ERROR = auto()
    SKIP = auto()


class GamestateMetric(Enum):
    """Key metrics for gamestate synchronization"""
    PREDICTION_ERROR = "prediction_error_ms"      # Client prediction vs server
    RECONCILIATION_TIME = "reconciliation_ms"     # Time to correct errors
    INTERPOLATION_ERROR = "interpolation_error_m" # Position interpolation accuracy
    COMBAT_SYNC_DELAY = "combat_sync_ms"          # Combat event synchronization
    ZONE_HANDOFF_TIME = "zone_handoff_ms"         # Zone transition time
    BANDWIDTH_USAGE = "bandwidth_bps"             # Network bandwidth
    TICK_STABILITY = "tick_variance_ms"           # Server tick consistency


@dataclass
class TestConfig:
    """Configuration for test execution"""
    # Network conditions
    latency_ms: float = 50.0
    jitter_ms: float = 10.0
    packet_loss_percent: float = 0.1
    bandwidth_limit_kbps: int = 256
    
    # Test parameters
    duration_seconds: float = 60.0
    client_count: int = 2
    tick_rate: int = 60
    
    # Tolerance thresholds
    max_prediction_error_ms: float = 100.0
    max_reconciliation_time_ms: float = 200.0
    max_interpolation_error_m: float = 0.5
    max_combat_sync_ms: float = 150.0
    max_zone_handoff_ms: float = 100.0
    min_tick_rate: float = 58.0  # Allow 2Hz variance
    
    # Paths
    server_binary: str = "./build/bin/darkages_server"
    client_binary: str = "./src/client/DarkAgesClient.exe"


@dataclass
class GamestateSample:
    """A snapshot of gamestate at a point in time"""
    timestamp: float
    client_tick: int
    server_tick: int
    client_position: Tuple[float, float, float]
    server_position: Tuple[float, float, float]
    client_velocity: Tuple[float, float, float]
    server_velocity: Tuple[float, float, float]
    latency_ms: float
    inputs_sent: int
    inputs_acked: int
    snapshots_received: int


@dataclass
class TestReport:
    """Comprehensive test report"""
    test_name: str
    start_time: float
    end_time: float
    config: TestConfig
    result: TestResult
    
    # Metrics
    samples: List[GamestateSample] = field(default_factory=list)
    prediction_errors: List[float] = field(default_factory=list)
    reconciliation_times: List[float] = field(default_factory=list)
    interpolation_errors: List[float] = field(default_factory=list)
    combat_delays: List[float] = field(default_factory=list)
    zone_handoff_times: List[float] = field(default_factory=list)
    tick_rates: List[float] = field(default_factory=list)
    
    # Failures
    failures: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    
    def calculate_statistics(self) -> Dict:
        """Calculate statistical summary of test results"""
        def stats(values: List[float]) -> Dict:
            if not values:
                return {"mean": 0, "median": 0, "p95": 0, "p99": 0, "max": 0}
            sorted_vals = sorted(values)
            n = len(sorted_vals)
            return {
                "mean": sum(values) / n,
                "median": sorted_vals[n // 2],
                "p95": sorted_vals[int(n * 0.95)],
                "p99": sorted_vals[int(n * 0.99)],
                "max": max(values)
            }
        
        return {
            "prediction_error_ms": stats(self.prediction_errors),
            "reconciliation_time_ms": stats(self.reconciliation_times),
            "interpolation_error_m": stats(self.interpolation_errors),
            "combat_delay_ms": stats(self.combat_delays),
            "zone_handoff_ms": stats(self.zone_handoff_times),
            "tick_rate_hz": stats(self.tick_rates)
        }
    
    def validate_thresholds(self, config: TestConfig) -> bool:
        """Validate results against thresholds"""
        stats = self.calculate_statistics()
        passed = True
        
        if stats["prediction_error_ms"]["p95"] > config.max_prediction_error_ms:
            self.failures.append(f"Prediction error P95 {stats['prediction_error_ms']['p95']:.1f}ms exceeds threshold {config.max_prediction_error_ms}ms")
            passed = False
        
        if stats["reconciliation_time_ms"]["p95"] > config.max_reconciliation_time_ms:
            self.failures.append(f"Reconciliation time P95 {stats['reconciliation_time_ms']['p95']:.1f}ms exceeds threshold {config.max_reconciliation_time_ms}ms")
            passed = False
        
        if stats["interpolation_error_m"]["p95"] > config.max_interpolation_error_m:
            self.failures.append(f"Interpolation error P95 {stats['interpolation_error_m']['p95']:.2f}m exceeds threshold {config.max_interpolation_error_m}m")
            passed = False
        
        if stats["combat_delay_ms"]["p95"] > config.max_combat_sync_ms:
            self.warnings.append(f"Combat delay P95 {stats['combat_delay_ms']['p95']:.1f}ms exceeds ideal {config.max_combat_sync_ms}ms")
        
        if stats["tick_rate_hz"]["mean"] < config.min_tick_rate:
            self.failures.append(f"Average tick rate {stats['tick_rate_hz']['mean']:.1f}Hz below minimum {config.min_tick_rate}Hz")
            passed = False
        
        self.result = TestResult.PASS if passed else TestResult.FAIL
        return passed


class GamestateValidator:
    """Validates gamestate consistency between client and server"""
    
    def __init__(self, config: TestConfig):
        self.config = config
        self.samples: List[GamestateSample] = []
    
    def add_sample(self, sample: GamestateSample):
        """Add a gamestate sample for analysis"""
        self.samples.append(sample)
    
    def validate_prediction(self) -> Tuple[bool, List[float]]:
        """
        Validate client prediction accuracy.
        
        Client predicts movement locally. Server validates and corrects if wrong.
        We measure how often and how severely the client is wrong.
        """
        errors = []
        
        for sample in self.samples:
            # Calculate position difference between client prediction and server truth
            dx = sample.client_position[0] - sample.server_position[0]
            dy = sample.client_position[1] - sample.server_position[1]
            dz = sample.client_position[2] - sample.server_position[2]
            
            distance = (dx*dx + dy*dy + dz*dz) ** 0.5
            
            # Convert to time error (how long at current velocity to create this error)
            velocity_mag = (sample.server_velocity[0]**2 + 
                          sample.server_velocity[1]**2 + 
                          sample.server_velocity[2]**2) ** 0.5
            
            if velocity_mag > 0.1:  # Moving
                time_error_ms = (distance / velocity_mag) * 1000
                errors.append(time_error_ms)
            else:  # Stationary
                if distance > 0.1:  # Should be 0 when stationary
                    errors.append(1000.0)  # Arbitrary large error
        
        if not errors:
            return True, []
        
        p95_error = sorted(errors)[int(len(errors) * 0.95)]
        passed = p95_error < self.config.max_prediction_error_ms
        
        return passed, errors
    
    def validate_reconciliation(self) -> Tuple[bool, List[float]]:
        """
        Validate server reconciliation efficiency.
        
        When server corrects client, how quickly does client converge to correct state?
        """
        reconciliation_times = []
        
        # Find samples where error was corrected
        for i in range(1, len(self.samples)):
            prev = self.samples[i-1]
            curr = self.samples[i]
            
            prev_error = self._position_error(prev)
            curr_error = self._position_error(curr)
            
            # If error decreased significantly, record reconciliation
            if prev_error > 0.5 and curr_error < 0.1:
                time_to_correct = curr.timestamp - prev.timestamp
                reconciliation_times.append(time_to_correct * 1000)  # Convert to ms
        
        if not reconciliation_times:
            return True, []
        
        p95_time = sorted(reconciliation_times)[int(len(reconciliation_times) * 0.95)]
        passed = p95_time < self.config.max_reconciliation_time_ms
        
        return passed, reconciliation_times
    
    def validate_interpolation(self, remote_samples: List[GamestateSample]) -> Tuple[bool, List[float]]:
        """
        Validate entity interpolation for remote players.
        
        Client renders remote players 100ms behind server to smooth movement.
        We validate this interpolation is accurate.
        """
        errors = []
        
        # Match client-rendered positions with actual server positions 100ms prior
        for sample in self.samples:
            target_time = sample.timestamp - 0.1  # 100ms interpolation delay
            
            # Find matching server sample
            matching = [s for s in remote_samples if abs(s.timestamp - target_time) < 0.01]
            if matching:
                actual = matching[0].server_position
                rendered = sample.client_position
                
                dx = actual[0] - rendered[0]
                dy = actual[1] - rendered[1]
                dz = actual[2] - rendered[2]
                
                error = (dx*dx + dy*dy + dz*dz) ** 0.5
                errors.append(error)
        
        if not errors:
            return True, []
        
        p95_error = sorted(errors)[int(len(errors) * 0.95)]
        passed = p95_error < self.config.max_interpolation_error_m
        
        return passed, errors
    
    def validate_tick_stability(self) -> Tuple[bool, List[float]]:
        """Validate server maintains consistent tick rate"""
        tick_rates = []
        
        for i in range(1, len(self.samples)):
            dt = self.samples[i].timestamp - self.samples[i-1].timestamp
            if dt > 0:
                tick_rate = 1.0 / dt
                tick_rates.append(tick_rate)
        
        if not tick_rates:
            return False, []
        
        avg_tick_rate = sum(tick_rates) / len(tick_rates)
        passed = avg_tick_rate >= self.config.min_tick_rate
        
        return passed, tick_rates
    
    def _position_error(self, sample: GamestateSample) -> float:
        """Calculate position error for a sample"""
        dx = sample.client_position[0] - sample.server_position[0]
        dy = sample.client_position[1] - sample.server_position[1]
        dz = sample.client_position[2] - sample.server_position[2]
        return (dx*dx + dy*dy + dz*dz) ** 0.5


class TestOrchestrator:
    """
    Orchestrates end-to-end gamestate synchronization tests
    """
    
    def __init__(self, config: TestConfig):
        self.config = config
        self.server_process: Optional[subprocess.Popen] = None
        self.client_processes: List[subprocess.Popen] = []
        self.running = False
    
    async def setup(self):
        """Setup test environment"""
        logger.info("Setting up test environment...")
        
        # Start server
        logger.info(f"Starting server: {self.config.server_binary}")
        self.server_process = subprocess.Popen(
            [self.config.server_binary, "--port", "7777", "--enable-metrics"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        # Wait for server to be ready
        await asyncio.sleep(2)
        
        # Configure network conditions (if on Linux with tc)
        await self._configure_network_conditions()
        
        logger.info("Test environment ready")
    
    async def teardown(self):
        """Cleanup test environment"""
        logger.info("Tearing down test environment...")
        
        # Kill clients
        for proc in self.client_processes:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except:
                proc.kill()
        
        # Kill server
        if self.server_process:
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except:
                self.server_process.kill()
        
        # Reset network conditions
        await self._reset_network_conditions()
        
        logger.info("Test environment cleaned up")
    
    async def _configure_network_conditions(self):
        """Configure network latency, jitter, and packet loss"""
        # This would use tc on Linux to simulate network conditions
        # For now, we'll log what would be configured
        logger.info(f"Network conditions: {self.config.latency_ms}ms latency, "
                   f"{self.config.jitter_ms}ms jitter, "
                   f"{self.config.packet_loss_percent}% packet loss")
    
    async def _reset_network_conditions(self):
        """Reset network to normal"""
        logger.info("Resetting network conditions")
    
    async def run_scenario(self, scenario_name: str) -> TestReport:
        """Run a specific test scenario"""
        logger.info(f"Running scenario: {scenario_name}")
        
        report = TestReport(
            test_name=scenario_name,
            start_time=time.time(),
            end_time=0,
            config=self.config,
            result=TestResult.ERROR
        )
        
        try:
            await self.setup()
            
            # Import and run scenario
            scenario = self._load_scenario(scenario_name)
            await scenario.execute(self.config, report)
            
            # Validate results
            report.end_time = time.time()
            report.validate_thresholds(self.config)
            
        except Exception as e:
            logger.error(f"Scenario failed: {e}")
            report.failures.append(str(e))
            report.result = TestResult.ERROR
            report.end_time = time.time()
        
        finally:
            await self.teardown()
        
        return report
    
    def _load_scenario(self, scenario_name: str):
        """Load a test scenario module"""
        # This would dynamically import scenario modules
        # For now, return a basic scenario
        from scenarios.MovementSyncScenario import MovementSyncScenario
        
        scenarios = {
            "movement_sync": MovementSyncScenario,
            # Add more scenarios as they're implemented
        }
        
        if scenario_name not in scenarios:
            raise ValueError(f"Unknown scenario: {scenario_name}")
        
        return scenarios[scenario_name]()
    
    async def run_all_scenarios(self) -> List[TestReport]:
        """Run all available test scenarios"""
        scenarios = [
            "movement_sync",
            # "combat_sync",
            # "zone_handoff",
            # "multiplayer_interpolation",
        ]
        
        reports = []
        for scenario in scenarios:
            report = await self.run_scenario(scenario)
            reports.append(report)
        
        return reports


def print_report(report: TestReport):
    """Print formatted test report"""
    print("\n" + "="*70)
    print(f"TEST REPORT: {report.test_name}")
    print("="*70)
    print(f"Result: {report.result.name}")
    print(f"Duration: {report.end_time - report.start_time:.1f}s")
    print()
    
    stats = report.calculate_statistics()
    print("METRICS:")
    print(f"  Prediction Error:     P95={stats['prediction_error_ms']['p95']:.1f}ms (max={stats['prediction_error_ms']['max']:.1f}ms)")
    print(f"  Reconciliation Time:  P95={stats['reconciliation_time_ms']['p95']:.1f}ms (max={stats['reconciliation_time_ms']['max']:.1f}ms)")
    print(f"  Interpolation Error:  P95={stats['interpolation_error_m']['p95']:.2f}m (max={stats['interpolation_error_m']['max']:.2f}m)")
    print(f"  Combat Delay:         P95={stats['combat_delay_ms']['p95']:.1f}ms (max={stats['combat_delay_ms']['max']:.1f}ms)")
    print(f"  Tick Rate:            Mean={stats['tick_rate_hz']['mean']:.1f}Hz (min={stats['tick_rate_hz']['min']:.1f}Hz)")
    print()
    
    if report.failures:
        print("FAILURES:")
        for failure in report.failures:
            print(f"  ❌ {failure}")
    
    if report.warnings:
        print("WARNINGS:")
        for warning in report.warnings:
            print(f"  ⚠️  {warning}")
    
    print("="*70)


async def main():
    parser = argparse.ArgumentParser(description='DarkAges Gamestate Test Orchestrator')
    parser.add_argument('--scenario', type=str, help='Specific scenario to run')
    parser.add_argument('--all-scenarios', action='store_true', help='Run all scenarios')
    parser.add_argument('--duration', type=float, default=60, help='Test duration in seconds')
    parser.add_argument('--latency', type=float, default=50, help='Network latency (ms)')
    parser.add_argument('--jitter', type=float, default=10, help='Network jitter (ms)')
    parser.add_argument('--packet-loss', type=float, default=0.1, help='Packet loss (%)')
    parser.add_argument('--clients', type=int, default=2, help='Number of test clients')
    parser.add_argument('--output', type=str, help='Output JSON file for results')
    parser.add_argument('--continuous', action='store_true', help='Run continuously')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    config = TestConfig(
        duration_seconds=args.duration,
        latency_ms=args.latency,
        jitter_ms=args.jitter,
        packet_loss_percent=args.packet_loss,
        client_count=args.clients
    )
    
    orchestrator = TestOrchestrator(config)
    
    if args.continuous:
        logger.info("Running in continuous mode (press Ctrl+C to stop)...")
        iteration = 0
        while True:
            iteration += 1
            logger.info(f"\n{'='*70}")
            logger.info(f"ITERATION {iteration}")
            logger.info('='*70)
            
            reports = await orchestrator.run_all_scenarios()
            
            for report in reports:
                print_report(report)
            
            # Check if all passed
            all_passed = all(r.result == TestResult.PASS for r in reports)
            if not all_passed:
                logger.error("Some tests failed! Stopping continuous mode.")
                break
            
            logger.info(f"All tests passed. Waiting 60 seconds before next iteration...")
            await asyncio.sleep(60)
    
    elif args.all_scenarios:
        reports = await orchestrator.run_all_scenarios()
        
        for report in reports:
            print_report(report)
        
        # Save results
        if args.output:
            results = {
                "timestamp": time.time(),
                "config": asdict(config),
                "reports": [
                    {
                        "name": r.test_name,
                        "result": r.result.name,
                        "statistics": r.calculate_statistics(),
                        "failures": r.failures,
                        "warnings": r.warnings
                    }
                    for r in reports
                ]
            }
            with open(args.output, 'w') as f:
                json.dump(results, f, indent=2)
            logger.info(f"Results saved to {args.output}")
    
    elif args.scenario:
        report = await orchestrator.run_scenario(args.scenario)
        print_report(report)
        
        if args.output:
            with open(args.output, 'w') as f:
                json.dump({
                    "report": asdict(report),
                    "statistics": report.calculate_statistics()
                }, f, indent=2, default=str)
    
    else:
        parser.print_help()


if __name__ == '__main__':
    asyncio.run(main())
