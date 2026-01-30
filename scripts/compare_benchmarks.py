#!/usr/bin/env python3
"""
Compare Diagon and Lucene benchmark results side-by-side.

Generates a markdown report with speedup ratios and performance gaps.

Usage: python3 compare_benchmarks.py diagon.csv lucene.csv > COMPARISON.md
"""

import sys
import csv
from pathlib import Path


def load_results(csv_path):
    """
    Load benchmark results from CSV.

    Args:
        csv_path: Path to CSV file (benchmark,time_ns,stddev_ns)

    Returns:
        Dictionary mapping benchmark name to (time, stddev) tuple
    """
    results = {}

    try:
        with open(csv_path, 'r') as fp:
            reader = csv.DictReader(fp)
            for row in reader:
                name = row['benchmark']
                time_ns = float(row['time_ns'])
                stddev_ns = float(row.get('stddev_ns', 0))
                results[name] = (time_ns, stddev_ns)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}", file=sys.stderr)
        sys.exit(1)

    return results


def compare_results(diagon_results, lucene_results):
    """
    Compare Diagon and Lucene results.

    Returns:
        List of tuples: (benchmark, diagon_time, lucene_time, speedup, gap_pct)
    """
    comparisons = []

    # Match benchmarks by name (may need fuzzy matching)
    all_benchmarks = set(diagon_results.keys()) | set(lucene_results.keys())

    for name in sorted(all_benchmarks):
        if name not in diagon_results:
            print(f"Warning: Benchmark '{name}' only in Lucene", file=sys.stderr)
            continue
        if name not in lucene_results:
            print(f"Warning: Benchmark '{name}' only in Diagon", file=sys.stderr)
            continue

        diagon_time, _ = diagon_results[name]
        lucene_time, _ = lucene_results[name]

        # Speedup = Lucene time / Diagon time
        # >1.0 means Diagon is faster
        speedup = lucene_time / diagon_time if diagon_time > 0 else float('inf')

        # Gap percentage: positive means Diagon is slower
        gap_pct = (diagon_time / lucene_time - 1.0) * 100 if lucene_time > 0 else 0

        comparisons.append((name, diagon_time, lucene_time, speedup, gap_pct))

    # Sort by speedup (worst to best)
    comparisons.sort(key=lambda x: x[3])

    return comparisons


def format_time(time_ns):
    """Format time for readability."""
    if time_ns < 1000:
        return f"{time_ns:.1f} ns"
    elif time_ns < 1000000:
        return f"{time_ns/1000:.1f} µs"
    elif time_ns < 1000000000:
        return f"{time_ns/1000000:.1f} ms"
    else:
        return f"{time_ns/1000000000:.2f} s"


def generate_report(comparisons):
    """Generate markdown report."""
    print("# Diagon vs Lucene Benchmark Comparison\n")
    print(f"*Generated: {Path.cwd()}*\n")

    # Summary statistics
    total = len(comparisons)
    diagon_faster = sum(1 for c in comparisons if c[3] > 1.0)
    lucene_faster = total - diagon_faster
    avg_speedup = sum(c[3] for c in comparisons) / total if total > 0 else 0

    print("## Summary\n")
    print(f"- **Benchmarks compared**: {total}")
    print(f"- **Diagon faster**: {diagon_faster} ({diagon_faster/total*100:.1f}%)")
    print(f"- **Lucene faster**: {lucene_faster} ({lucene_faster/total*100:.1f}%)")
    print(f"- **Average speedup**: {avg_speedup:.2f}x")
    print("")

    # Interpretation
    if avg_speedup > 1.1:
        print("✅ **Diagon outperforms Lucene overall**")
    elif avg_speedup < 0.9:
        print("❌ **Diagon underperforms Lucene overall**")
    else:
        print("⚖️ **Diagon and Lucene have comparable performance**")
    print("")

    # Detailed results
    print("## Performance Gaps\n")
    print("| Status | Benchmark | Diagon | Lucene | Speedup | Gap |")
    print("|--------|-----------|--------|--------|---------|-----|")

    for name, diagon_time, lucene_time, speedup, gap_pct in comparisons:
        # Status emoji
        if speedup >= 1.2:
            status = "✅"
        elif speedup >= 1.0:
            status = "🟢"
        elif speedup >= 0.9:
            status = "🟡"
        else:
            status = "❌"

        # Format times
        diagon_str = format_time(diagon_time)
        lucene_str = format_time(lucene_time)

        # Gap string
        if gap_pct > 0:
            gap_str = f"+{gap_pct:.1f}%"
        else:
            gap_str = f"{gap_pct:.1f}%"

        print(f"| {status} | {name} | {diagon_str} | {lucene_str} | "
              f"{speedup:.2f}x | {gap_str} |")

    # Bottleneck identification
    print("\n## Identified Bottlenecks (>20% slower)\n")
    bottlenecks = [(n, d, l, s, g) for n, d, l, s, g in comparisons if g > 20]

    if bottlenecks:
        for name, diagon_time, lucene_time, speedup, gap_pct in bottlenecks:
            print(f"- **{name}**: {gap_pct:.1f}% slower ({speedup:.2f}x)")
        print("")
        print(f"**Priority**: Fix these {len(bottlenecks)} bottlenecks first (P0)")
    else:
        print("✅ No major bottlenecks identified")

    print("")

    # Strengths
    print("## Diagon Strengths\n")
    strengths = [(n, d, l, s, g) for n, d, l, s, g in comparisons if s > 1.1]

    if strengths:
        for name, diagon_time, lucene_time, speedup, gap_pct in strengths[:5]:
            print(f"- **{name}**: {speedup:.2f}x faster")
    else:
        print("No significant performance advantages identified yet")

    print("")

    # Recommendations
    print("## Next Steps\n")
    print("1. **Profile bottlenecks**: Run `perf` profiling on slow benchmarks")
    print("2. **Micro-benchmark**: Isolate bottleneck components")
    print("3. **Optimize**: Implement fixes (see OPTIMIZATION_LOG.md)")
    print("4. **Validate**: Re-run benchmarks to measure improvement")
    print("")


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <diagon.csv> <lucene.csv>", file=sys.stderr)
        print("", file=sys.stderr)
        print("Compare Diagon and Lucene benchmark results.", file=sys.stderr)
        print("Generates markdown report to stdout.", file=sys.stderr)
        sys.exit(1)

    diagon_csv = sys.argv[1]
    lucene_csv = sys.argv[2]

    # Check files exist
    for f in [diagon_csv, lucene_csv]:
        if not Path(f).exists():
            print(f"Error: File not found: {f}", file=sys.stderr)
            sys.exit(1)

    # Load results
    diagon_results = load_results(diagon_csv)
    lucene_results = load_results(lucene_csv)

    if not diagon_results:
        print("Error: No Diagon results found", file=sys.stderr)
        sys.exit(1)

    if not lucene_results:
        print("Error: No Lucene results found", file=sys.stderr)
        sys.exit(1)

    # Compare
    comparisons = compare_results(diagon_results, lucene_results)

    if not comparisons:
        print("Error: No matching benchmarks found", file=sys.stderr)
        sys.exit(1)

    # Generate report
    generate_report(comparisons)

    # Also save CSV for further analysis
    csv_path = Path("comparison_summary.csv")
    with open(csv_path, 'w', newline='') as fp:
        writer = csv.writer(fp)
        writer.writerow(['benchmark', 'diagon_time_ns', 'lucene_time_ns', 'speedup', 'gap_pct'])
        for row in comparisons:
            writer.writerow(row)

    print(f"*CSV saved to: {csv_path.absolute()}*", file=sys.stderr)


if __name__ == '__main__':
    main()
