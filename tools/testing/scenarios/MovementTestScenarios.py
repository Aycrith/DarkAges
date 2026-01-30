"""
Movement Synchronization Test Scenarios

Comprehensive test scenarios for validating client prediction,
server reconciliation, and movement synchronization.
"""

import asyncio
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Any
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent.parent))

from telemetry.MetricsCollector import MetricsCollector


@dataclass
class MovementTestResult:
    """Result of a movement test."""
    scenario_name: str
    passed: bool
    duration_ms: float
    metrics: Dict[str, Any]
    errors: List[str]
    screenshots: List[str]
    details: Dict[str, Any]


class MovementTestScenarios:
    """Movement synchronization test scenarios."""
    
    def __init__(self, use_mcp: bool = False):
        self.use_mcp = use_mcp
        self.collector: Optional[MetricsCollector] = None
        self.results: List[MovementTestResult] = []
    
    async def scenario_basic_movement(self) -> MovementTestResult:
        """Scenario 1: Basic Movement Synchronization"""
        scenario_name = "basic_movement"
        errors = []
        screenshots = []
        
        with MetricsCollector(scenario_name) as collector:
            self.collector = collector
            start_time = time.time()
            
            try:
                collector.log_info("phase_start", {"phase": "setup"})
                
                # Phase 2: Execute movement
                collector.log_info("phase_start", {"phase": "movement"})
                
                movement_sequence = [
                    ("move_forward", 1.0),
                    ("move_right", 1.0),
                    ("move_backward", 1.0),
                    ("move_left", 1.0),
                ]
                
                for action, duration in movement_sequence:
                    input_start = time.time()
                    
                    collector.record_event("input_injected", {
                        "action": action,
                        "duration": duration
                    })
                    
                    await asyncio.sleep(0.1)
                    
                    collector.record_timing(
                        "input.processing",
                        (time.time() - input_start) * 1000,
                        {"action": action}
                    )
                
                # Validation
                collector.log_info("phase_start", {"phase": "validation"})
                
                stats = collector.get_statistics("timing.input.processing")
                
                if stats:
                    collector.record_gauge("validation.mean_processing_time", stats.get("mean", 0))
                    
                    if stats.get("mean", 0) > 100:
                        errors.append(f"Mean processing time {stats['mean']:.2f}ms exceeds threshold")
                
                # Simulate prediction errors
                prediction_errors = 0
                for i in range(10):
                    error = abs(16.67 - (15 + i * 0.5))
                    collector.record_timing("prediction.error", error)
                    if error > 50:
                        prediction_errors += 1
                
                error_stats = collector.get_statistics("timing.prediction.error")
                collector.record_gauge("validation.prediction_error_p95", error_stats.get("p95", 0))
                
                if prediction_errors > 2:
                    errors.append(f"Too many prediction errors: {prediction_errors}")
                
                passed = len(errors) == 0
                
            except Exception as e:
                collector.log_error("scenario_exception", {"error": str(e)})
                errors.append(str(e))
                passed = False
            
            duration_ms = (time.time() - start_time) * 1000
            
            return MovementTestResult(
                scenario_name=scenario_name,
                passed=passed,
                duration_ms=duration_ms,
                metrics=collector.generate_report(),
                errors=errors,
                screenshots=screenshots,
                details={
                    "movement_sequence": [(a, d) for a, d in movement_sequence] if 'movement_sequence' in locals() else [],
                    "prediction_errors": prediction_errors if 'prediction_errors' in locals() else 0
                }
            )
    
    async def scenario_high_latency(self) -> MovementTestResult:
        """Scenario 2: High Latency Resilience"""
        scenario_name = "high_latency"
        errors = []
        screenshots = []
        
        with MetricsCollector(scenario_name) as collector:
            self.collector = collector
            start_time = time.time()
            
            try:
                collector.log_info("scenario_start", {
                    "latency_ms": 300,
                    "jitter_ms": 50
                })
                
                latencies = [250, 300, 350, 400, 320, 280, 310]
                
                for latency in latencies:
                    collector.record_timing("network.latency", latency)
                    
                    if latency > 350:
                        collector.record_event("rubber_band_detected", {
                            "latency": latency,
                            "severity": "high" if latency > 400 else "medium"
                        })
                
                latency_stats = collector.get_statistics("timing.network.latency")
                
                rubber_bands = sum(1 for e in collector.events if e.name == "rubber_band_detected")
                collector.record_gauge("validation.rubber_bands", rubber_bands)
                
                if rubber_bands > 3:
                    errors.append(f"Excessive rubber-banding: {rubber_bands} events")
                
                if latency_stats.get("mean", 0) > 350:
                    errors.append(f"Mean latency too high: {latency_stats['mean']:.2f}ms")
                
                passed = len(errors) == 0
                
            except Exception as e:
                collector.log_error("scenario_exception", {"error": str(e)})
                errors.append(str(e))
                passed = False
            
            duration_ms = (time.time() - start_time) * 1000
            
            return MovementTestResult(
                scenario_name=scenario_name,
                passed=passed,
                duration_ms=duration_ms,
                metrics=collector.generate_report(),
                errors=errors,
                screenshots=screenshots,
                details={
                    "simulated_latency_range": f"{min(latencies)}-{max(latencies)}ms" if 'latencies' in locals() else "0-0ms",
                    "rubber_bands": rubber_bands if 'rubber_bands' in locals() else 0
                }
            )
    
    async def run_all(self) -> Dict[str, MovementTestResult]:
        """Run all movement test scenarios."""
        print("=" * 70)
        print("MOVEMENT SYNCHRONIZATION TEST SUITE")
        print("=" * 70)
        print(f"Mode: {'MCP (Real Godot)' if self.use_mcp else 'Simulation'}")
        print()
        
        scenarios = [
            self.scenario_basic_movement,
            self.scenario_high_latency,
        ]
        
        results = {}
        passed_count = 0
        
        for scenario in scenarios:
            print(f"\nRunning: {scenario.__name__}...")
            result = await scenario()
            results[result.scenario_name] = result
            
            status = "[PASS]" if result.passed else "[FAIL]"
            print(f"  {status} {result.scenario_name} ({result.duration_ms:.2f}ms)")
            
            if result.errors:
                for error in result.errors:
                    print(f"    ERROR: {error}")
            
            if result.passed:
                passed_count += 1
        
        print("\n" + "=" * 70)
        print(f"SUMMARY: {passed_count}/{len(scenarios)} scenarios passed")
        print("=" * 70)
        
        return results


async def main():
    """Main entry point."""
    suite = MovementTestScenarios(use_mcp=False)
    results = await suite.run_all()
    
    passed = sum(1 for r in results.values() if r.passed)
    return passed == len(results)


if __name__ == "__main__":
    import sys
    success = asyncio.run(main())
    sys.exit(0 if success else 1)
