#!/usr/bin/env python3
"""
Redis Performance Regression Detection Script

Compares current benchmark results against baseline metrics to detect performance regressions.

Usage:
    python scripts/check_performance_regression.py <test_output_file>

Exit Codes:
    0: No regression detected
    1: Warning-level regression (>10% degradation but within thresholds)
    2: Critical regression (exceeds thresholds or >20% degradation)
"""

import sys
import json
import re
from pathlib import Path
from typing import Dict, Tuple, Optional
from dataclasses import dataclass
from enum import Enum


class RegressionLevel(Enum):
    """Severity level of detected regressions"""

    NONE = 0
    WARNING = 1
    CRITICAL = 2


@dataclass
class Metric:
    """Represents a performance metric with its value and unit"""

    name: str
    value: float
    unit: str
    baseline: Optional[float] = None

    def change_pct(self) -> Optional[float]:
        """Calculate percentage change from baseline"""
        if self.baseline is None or self.baseline == 0:
            return None
        return ((self.value - self.baseline) / self.baseline) * 100

    def is_latency_metric(self) -> bool:
        """Check if this is a latency metric (lower is better)"""
        return (
            "latency" in self.name.lower() or "_us" in self.name or "_ms" in self.name
        )

    def is_throughput_metric(self) -> bool:
        """Check if this is a throughput metric (higher is better)"""
        return "throughput" in self.name.lower() or "ops" in self.name


class PerformanceChecker:
    """Main performance regression checker"""

    def __init__(self, baseline_path: str = "docs/performance/baseline.json"):
        self.baseline_path = Path(baseline_path)
        self.baseline = self.load_baseline()
        self.current_metrics: Dict[str, Metric] = {}
        self.regressions: Dict[RegressionLevel, list] = {
            RegressionLevel.NONE: [],
            RegressionLevel.WARNING: [],
            RegressionLevel.CRITICAL: [],
        }
        # Set console encoding to UTF-8 for Windows
        import sys

        if sys.platform == "win32":
            import codecs

            sys.stdout = codecs.getwriter("utf-8")(sys.stdout.buffer, "strict")
            sys.stderr = codecs.getwriter("utf-8")(sys.stderr.buffer, "strict")

    def load_baseline(self) -> dict:
        """Load baseline metrics from JSON file"""
        if not self.baseline_path.exists():
            print(f"[WARN] Baseline file not found: {self.baseline_path}")
            print("       Creating baseline from current run...")
            return {
                "metrics": {},
                "thresholds": {
                    "latency_regression_pct": 20,
                    "throughput_regression_pct": 10,
                    "critical_latency_ms": 10,
                    "critical_throughput_min_ops_sec": 10000,
                },
            }

        with open(self.baseline_path, "r") as f:
            return json.load(f)

    def parse_test_output(self, output_file: Path) -> None:
        """Parse test output file to extract performance metrics"""
        if not output_file.exists():
            raise FileNotFoundError(f"Test output file not found: {output_file}")

        with open(output_file, "r") as f:
            content = f.read()

        # Parse various metric formats
        self._parse_latency_metrics(content)
        self._parse_throughput_metrics(content)
        self._parse_pipeline_metrics(content)

    def _parse_latency_metrics(self, content: str) -> None:
        """Extract latency metrics from test output"""
        # Pattern: "Average latency: 620 μs" or "Average latency: 620μs"
        latency_pattern = r"Average latency:\s*(\d+(?:\.\d+)?)\s*[μu]s"
        matches = re.findall(latency_pattern, content, re.IGNORECASE)

        # Map to metric names (simplified - in production, parse from context)
        metric_names = [
            "set_latency_us",
            "get_latency_us",
            "del_latency_us",
            "xadd_latency_us",
            "xread_latency_us",
        ]

        for idx, match in enumerate(matches[: len(metric_names)]):
            metric_name = metric_names[idx]
            value = float(match)
            baseline_value = self.baseline["metrics"].get(metric_name)

            self.current_metrics[metric_name] = Metric(
                name=metric_name, value=value, unit="μs", baseline=baseline_value
            )

    def _parse_throughput_metrics(self, content: str) -> None:
        """Extract throughput metrics from test output"""
        # Pattern: "Operations/sec: 227273" or "227,273 ops/sec"
        throughput_pattern = r"(\d+(?:,\d+)?)\s*ops?/sec"
        matches = re.findall(throughput_pattern, content, re.IGNORECASE)

        if matches:
            # Remove commas and convert to int
            value = float(matches[0].replace(",", ""))
            baseline_value = self.baseline["metrics"].get("throughput_ops_sec")

            self.current_metrics["throughput_ops_sec"] = Metric(
                name="throughput_ops_sec",
                value=value,
                unit="ops/sec",
                baseline=baseline_value,
            )

    def _parse_pipeline_metrics(self, content: str) -> None:
        """Extract pipeline performance metrics"""
        # Pattern: "Pipeline throughput: 227273 ops/sec"
        pipeline_pattern = r"Pipeline throughput:\s*(\d+(?:,\d+)?)\s*ops/sec"
        match = re.search(pipeline_pattern, content, re.IGNORECASE)

        if match:
            value = float(match.group(1).replace(",", ""))

            # Calculate per-op latency
            # If throughput is 227,273 ops/sec, each op takes 1/227273 seconds = 4.4 microseconds
            latency_per_op = (1.0 / value) * 1_000_000  # Convert to microseconds

            baseline_value = self.baseline["metrics"].get("pipeline_latency_per_op_us")

            self.current_metrics["pipeline_latency_per_op_us"] = Metric(
                name="pipeline_latency_per_op_us",
                value=latency_per_op,
                unit="μs",
                baseline=baseline_value,
            )

    def check_regressions(self) -> RegressionLevel:
        """Check all metrics for regressions"""
        thresholds = self.baseline.get("thresholds", {})
        latency_threshold = thresholds.get("latency_regression_pct", 20)
        throughput_threshold = thresholds.get("throughput_regression_pct", 10)
        critical_latency_ms = thresholds.get("critical_latency_ms", 10)

        max_severity = RegressionLevel.NONE

        for metric_name, metric in self.current_metrics.items():
            if metric.baseline is None:
                continue

            change_pct = metric.change_pct()
            if change_pct is None:
                continue

            severity = self._check_metric_regression(
                metric,
                change_pct,
                latency_threshold,
                throughput_threshold,
                critical_latency_ms,
            )

            if severity.value > max_severity.value:
                max_severity = severity

        return max_severity

    def _check_metric_regression(
        self,
        metric: Metric,
        change_pct: float,
        latency_threshold: float,
        throughput_threshold: float,
        critical_latency_ms: float,
    ) -> RegressionLevel:
        """Check a single metric for regression"""

        # Latency metrics (lower is better)
        if metric.is_latency_metric():
            # Check critical threshold (absolute value)
            if metric.unit == "μs" and metric.value > critical_latency_ms * 1000:
                msg = f"[FAIL] {metric.name}: {metric.value:.1f}{metric.unit} (baseline: {metric.baseline:.1f}{metric.unit}, change: +{change_pct:.1f}%) - EXCEEDS {critical_latency_ms}ms LIMIT"
                self.regressions[RegressionLevel.CRITICAL].append(msg)
                return RegressionLevel.CRITICAL

            # Check percentage degradation (increase is bad for latency)
            if change_pct > latency_threshold:
                msg = f"[FAIL] {metric.name}: {metric.value:.1f}{metric.unit} (baseline: {metric.baseline:.1f}{metric.unit}, change: +{change_pct:.1f}%)"
                self.regressions[RegressionLevel.CRITICAL].append(msg)
                return RegressionLevel.CRITICAL
            elif change_pct > latency_threshold / 2:
                msg = f"[WARN] {metric.name}: {metric.value:.1f}{metric.unit} (baseline: {metric.baseline:.1f}{metric.unit}, change: +{change_pct:.1f}%)"
                self.regressions[RegressionLevel.WARNING].append(msg)
                return RegressionLevel.WARNING

        # Throughput metrics (higher is better)
        elif metric.is_throughput_metric():
            # Check percentage degradation (decrease is bad for throughput)
            if change_pct < -throughput_threshold:
                msg = f"[FAIL] {metric.name}: {metric.value:.0f} {metric.unit} (baseline: {metric.baseline:.0f}, change: {change_pct:.1f}%)"
                self.regressions[RegressionLevel.CRITICAL].append(msg)
                return RegressionLevel.CRITICAL
            elif change_pct < -throughput_threshold / 2:
                msg = f"[WARN] {metric.name}: {metric.value:.0f} {metric.unit} (baseline: {metric.baseline:.0f}, change: {change_pct:.1f}%)"
                self.regressions[RegressionLevel.WARNING].append(msg)
                return RegressionLevel.WARNING

        # Throughput metrics (higher is better)
        elif metric.is_throughput_metric():
            # Check percentage degradation (decrease is bad for throughput)
            if change_pct < -throughput_threshold:
                msg = f"❌ {metric.name}: {metric.value:.0f} {metric.unit} (baseline: {metric.baseline:.0f}, change: {change_pct:.1f}%)"
                self.regressions[RegressionLevel.CRITICAL].append(msg)
                return RegressionLevel.CRITICAL
            elif change_pct < -throughput_threshold / 2:
                msg = f"⚠️  {metric.name}: {metric.value:.0f} {metric.unit} (baseline: {metric.baseline:.0f}, change: {change_pct:.1f}%)"
                self.regressions[RegressionLevel.WARNING].append(msg)
                return RegressionLevel.WARNING

        # No regression
        return RegressionLevel.NONE

    def print_results(self, severity: RegressionLevel) -> None:
        """Print regression check results"""
        print("\n" + "=" * 60)
        print("Redis Performance Regression Check")
        print("=" * 60)

        # Print all metrics
        print("\nCurrent Metrics vs Baseline:\n")
        for metric_name, metric in sorted(self.current_metrics.items()):
            if metric.baseline is not None:
                change_pct = metric.change_pct()
                change_str = f"{change_pct:+.1f}%" if change_pct is not None else "N/A"

                # Color code based on metric type and change
                if metric.is_latency_metric():
                    symbol = (
                        "[REGR]"
                        if change_pct > 10
                        else "[GOOD]"
                        if change_pct < -10
                        else "[ OK ]"
                    )
                elif metric.is_throughput_metric():
                    symbol = (
                        "[REGR]"
                        if change_pct < -10
                        else "[GOOD]"
                        if change_pct > 10
                        else "[ OK ]"
                    )
                else:
                    symbol = "[INFO]"

                print(
                    f"{symbol} {metric.name:30s}: {metric.value:>10.1f} {metric.unit:8s} "
                    f"(baseline: {metric.baseline:>10.1f}, change: {change_str})"
                )
            else:
                print(
                    f"[NEW ] {metric.name:30s}: {metric.value:>10.1f} {metric.unit:8s} (no baseline)"
                )

        # Print regressions
        if severity == RegressionLevel.NONE:
            print("\n[PASS] No performance regressions detected!")
            print("       All metrics within acceptable thresholds.")
        else:
            print(
                f"\n{'[WARN] WARNINGS' if severity == RegressionLevel.WARNING else '[FAIL] CRITICAL REGRESSIONS'} Detected:\n"
            )

            for level in [RegressionLevel.CRITICAL, RegressionLevel.WARNING]:
                if self.regressions[level]:
                    for msg in self.regressions[level]:
                        # Strip emoji from messages
                        msg = msg.replace("❌", "[FAIL]").replace("⚠️", "[WARN]")
                        print(f"  {msg}")

        print("\n" + "=" * 60)
        print(f"Overall Result: {severity.name}")
        print("=" * 60 + "\n")


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python check_performance_regression.py <test_output_file>")
        print("\nExample:")
        print("  python scripts/check_performance_regression.py build/perf_results.txt")
        sys.exit(2)

    output_file = Path(sys.argv[1])

    try:
        checker = PerformanceChecker()
        checker.parse_test_output(output_file)
        severity = checker.check_regressions()
        checker.print_results(severity)

        # Exit with appropriate code
        sys.exit(severity.value)

    except FileNotFoundError as e:
        print(f"[FAIL] Error: {e}", file=sys.stderr)
        sys.exit(2)
    except Exception as e:
        print(f"[FAIL] Unexpected error: {e}", file=sys.stderr)
        import traceback

        traceback.print_exc()
        sys.exit(2)
    except Exception as e:
        print(f"❌ Unexpected error: {e}", file=sys.stderr)
        import traceback

        traceback.print_exc()
        sys.exit(2)


if __name__ == "__main__":
    main()
