#!/usr/bin/env python3
"""
Aggregate Google Benchmark JSON results across multiple runs.

Calculates median and standard deviation for each benchmark,
producing a CSV suitable for comparison.

Usage: python3 aggregate_results.py run1.json run2.json ... > results.csv
Output: benchmark_name,median_time_ns,stddev_ns
"""

import json
import sys
import statistics
from pathlib import Path


def aggregate_benchmarks(files):
    """
    Aggregate benchmark results from multiple JSON files.

    Args:
        files: List of JSON file paths

    Returns:
        Dictionary mapping benchmark name to list of times
    """
    results = {}

    for file_path in files:
        try:
            with open(file_path, 'r') as fp:
                data = json.load(fp)

                if 'benchmarks' not in data:
                    print(f"Warning: No 'benchmarks' key in {file_path}", file=sys.stderr)
                    continue

                for benchmark in data['benchmarks']:
                    name = benchmark.get('name', 'unknown')

                    # Get real_time (wall-clock time)
                    if 'real_time' not in benchmark:
                        continue

                    real_time = benchmark['real_time']
                    time_unit = benchmark.get('time_unit', 'ns')

                    # Convert to nanoseconds for consistency
                    if time_unit == 'us':
                        real_time *= 1000
                    elif time_unit == 'ms':
                        real_time *= 1000000
                    elif time_unit == 's':
                        real_time *= 1000000000

                    if name not in results:
                        results[name] = []
                    results[name].append(real_time)

        except Exception as e:
            print(f"Error reading {file_path}: {e}", file=sys.stderr)
            continue

    return results


def calculate_statistics(results):
    """
    Calculate median and stddev for each benchmark.

    Args:
        results: Dictionary mapping name to list of times

    Returns:
        List of tuples (name, median, stddev)
    """
    stats = []

    for name, times in sorted(results.items()):
        if not times:
            continue

        median = statistics.median(times)
        stddev = statistics.stdev(times) if len(times) > 1 else 0.0

        stats.append((name, median, stddev))

    return stats


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file1.json> <file2.json> ...", file=sys.stderr)
        print("", file=sys.stderr)
        print("Aggregates Google Benchmark results across multiple runs.", file=sys.stderr)
        print("Outputs CSV: benchmark_name,median_time_ns,stddev_ns", file=sys.stderr)
        sys.exit(1)

    files = sys.argv[1:]

    # Check files exist
    for f in files:
        if not Path(f).exists():
            print(f"Error: File not found: {f}", file=sys.stderr)
            sys.exit(1)

    # Aggregate
    results = aggregate_benchmarks(files)

    if not results:
        print("Error: No benchmark results found in input files", file=sys.stderr)
        sys.exit(1)

    # Calculate statistics
    stats = calculate_statistics(results)

    # Output CSV
    print("benchmark,time_ns,stddev_ns")
    for name, median, stddev in stats:
        print(f"{name},{median:.2f},{stddev:.2f}")

    # Print summary to stderr
    print(f"\nProcessed {len(files)} files, {len(stats)} benchmarks", file=sys.stderr)


if __name__ == '__main__':
    main()
