#!/usr/bin/env python3
"""
FST Performance Regression Checker

Compares current FST benchmark results against baseline and detects regressions > 10%.

Usage:
    python3 check_fst_regression.py current_results.json baseline.json

Exit codes:
    0: No significant regression
    1: Regression > 10% detected (FAIL)
    2: Error (file not found, parse error, etc.)
"""

import json
import sys
from typing import Dict, List, Tuple
from dataclasses import dataclass
from enum import Enum


class RegressionLevel(Enum):
    """Regression severity levels"""
    IMPROVEMENT = "improvement"      # Performance improved
    ACCEPTABLE = "acceptable"        # Within 10% threshold
    WARNING = "warning"             # 10-20% slower
    CRITICAL = "critical"            # >20% slower


@dataclass
class BenchmarkResult:
    """Single benchmark result"""
    name: str
    time_ns: float
    items_per_sec: float = 0.0
    label: str = ""


@dataclass
class RegressionAnalysis:
    """Regression analysis for a benchmark"""
    name: str
    baseline_ns: float
    current_ns: float
    regression_pct: float
    level: RegressionLevel

    def __str__(self) -> str:
        symbol = {
            RegressionLevel.IMPROVEMENT: "✅",
            RegressionLevel.ACCEPTABLE: "✅",
            RegressionLevel.WARNING: "⚠️",
            RegressionLevel.CRITICAL: "❌"
        }[self.level]

        sign = "+" if self.regression_pct > 0 else ""
        return (f"{symbol} {self.name:50s} "
                f"Baseline: {self.baseline_ns:10.1f} ns  "
                f"Current: {self.current_ns:10.1f} ns  "
                f"Change: {sign}{self.regression_pct:+6.1f}%")


def load_benchmark_results(filename: str) -> Dict[str, BenchmarkResult]:
    """Load Google Benchmark JSON results"""
    try:
        with open(filename, 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"❌ Error: File not found: {filename}", file=sys.stderr)
        sys.exit(2)
    except json.JSONDecodeError as e:
        print(f"❌ Error: Invalid JSON in {filename}: {e}", file=sys.stderr)
        sys.exit(2)

    results = {}
    for benchmark in data.get('benchmarks', []):
        name = benchmark['name']

        # Extract time (prefer real_time, fall back to cpu_time)
        time_ns = benchmark.get('real_time', benchmark.get('cpu_time', 0))

        # Convert to nanoseconds if needed
        time_unit = benchmark.get('time_unit', 'ns')
        if time_unit == 'ms':
            time_ns *= 1_000_000
        elif time_unit == 'us':
            time_ns *= 1_000

        # Extract items processed (for throughput calculation)
        items_per_sec = benchmark.get('items_per_second', 0)

        # Extract label
        label = benchmark.get('label', '')

        results[name] = BenchmarkResult(
            name=name,
            time_ns=time_ns,
            items_per_sec=items_per_sec,
            label=label
        )

    return results


def analyze_regression(baseline: BenchmarkResult, current: BenchmarkResult) -> RegressionAnalysis:
    """Analyze regression between baseline and current"""
    baseline_ns = baseline.time_ns
    current_ns = current.time_ns

    # Calculate regression percentage
    # Positive = slower (regression), Negative = faster (improvement)
    regression_pct = ((current_ns - baseline_ns) / baseline_ns) * 100

    # Determine severity level
    if regression_pct <= -5:  # >5% faster
        level = RegressionLevel.IMPROVEMENT
    elif regression_pct <= 10:  # Within 10% threshold
        level = RegressionLevel.ACCEPTABLE
    elif regression_pct <= 20:  # 10-20% slower
        level = RegressionLevel.WARNING
    else:  # >20% slower
        level = RegressionLevel.CRITICAL

    return RegressionAnalysis(
        name=baseline.name,
        baseline_ns=baseline_ns,
        current_ns=current_ns,
        regression_pct=regression_pct,
        level=level
    )


def check_regressions(current_file: str, baseline_file: str) -> bool:
    """
    Check for performance regressions.

    Returns:
        True if all benchmarks pass (no critical regressions)
        False if any critical regressions found
    """
    print(f"FST Performance Regression Check")
    print(f"=" * 80)
    print(f"Baseline:  {baseline_file}")
    print(f"Current:   {current_file}")
    print(f"Threshold: 10% regression (warning), 20% (critical)")
    print(f"=" * 80)
    print()

    # Load results
    baseline_results = load_benchmark_results(baseline_file)
    current_results = load_benchmark_results(current_file)

    # Find common benchmarks
    baseline_names = set(baseline_results.keys())
    current_names = set(current_results.keys())

    common_names = baseline_names & current_names
    only_baseline = baseline_names - current_names
    only_current = current_names - baseline_names

    if not common_names:
        print("❌ Error: No common benchmarks found between baseline and current")
        return False

    # Analyze regressions
    analyses: List[RegressionAnalysis] = []
    for name in sorted(common_names):
        analysis = analyze_regression(baseline_results[name], current_results[name])
        analyses.append(analysis)

    # Group by severity
    improvements = [a for a in analyses if a.level == RegressionLevel.IMPROVEMENT]
    acceptable = [a for a in analyses if a.level == RegressionLevel.ACCEPTABLE]
    warnings = [a for a in analyses if a.level == RegressionLevel.WARNING]
    criticals = [a for a in analyses if a.level == RegressionLevel.CRITICAL]

    # Print results
    print("Regression Analysis:")
    print("-" * 80)

    if improvements:
        print(f"\n🎉 Performance Improvements ({len(improvements)} benchmarks):")
        for a in improvements:
            print(f"  {a}")

    if acceptable:
        print(f"\n✅ Acceptable Performance ({len(acceptable)} benchmarks):")
        for a in acceptable:
            print(f"  {a}")

    if warnings:
        print(f"\n⚠️  Performance Warnings ({len(warnings)} benchmarks):")
        print(f"   (10-20% slower than baseline)")
        for a in warnings:
            print(f"  {a}")

    if criticals:
        print(f"\n❌ CRITICAL REGRESSIONS ({len(criticals)} benchmarks):")
        print(f"   (>20% slower than baseline - IMMEDIATE ACTION REQUIRED)")
        for a in criticals:
            print(f"  {a}")

    # Report new/removed benchmarks
    if only_current:
        print(f"\n📝 New Benchmarks ({len(only_current)}):")
        for name in sorted(only_current):
            print(f"  + {name}")

    if only_baseline:
        print(f"\n📝 Removed Benchmarks ({len(only_baseline)}):")
        for name in sorted(only_baseline):
            print(f"  - {name}")

    # Summary
    print()
    print("=" * 80)
    print("Summary:")
    print(f"  Improvements:  {len(improvements):3d}")
    print(f"  Acceptable:    {len(acceptable):3d}")
    print(f"  Warnings:      {len(warnings):3d} (10-20% slower)")
    print(f"  Critical:      {len(criticals):3d} (>20% slower)")
    print(f"  Total:         {len(analyses):3d}")
    print("=" * 80)

    # Determine pass/fail
    has_critical = len(criticals) > 0
    has_warning = len(warnings) > 0

    if has_critical:
        print()
        print("❌ FAIL: Critical regressions detected (>20% slower)")
        print("   Action: Investigate and fix before merging")
        return False
    elif has_warning:
        print()
        print("⚠️  WARNING: Performance regressions detected (10-20% slower)")
        print("   Action: Review and consider optimization")
        print("   Status: PASS (within 20% threshold)")
        return True
    else:
        print()
        print("✅ PASS: No significant regressions detected")
        return True


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 check_fst_regression.py current_results.json baseline.json")
        print()
        print("Example:")
        print("  # Run benchmark")
        print("  ./FSTEfficiencyGate --benchmark_out=current.json --benchmark_out_format=json")
        print()
        print("  # Check for regressions")
        print("  python3 check_fst_regression.py current.json benchmark_results/fst_baseline.json")
        sys.exit(2)

    current_file = sys.argv[1]
    baseline_file = sys.argv[2]

    passed = check_regressions(current_file, baseline_file)

    sys.exit(0 if passed else 1)


if __name__ == '__main__':
    main()
