#!/usr/bin/env python3
"""
Generate performance trend report from history
"""

import json
import os
import sys
from pathlib import Path
from datetime import datetime

def load_history(history_dir):
    """Load all performance history files"""
    results = []

    if not os.path.exists(history_dir):
        print(f"No history directory found: {history_dir}")
        return results

    for filename in sorted(os.listdir(history_dir)):
        if filename.endswith('.json'):
            filepath = os.path.join(history_dir, filename)
            try:
                with open(filepath, 'r') as f:
                    data = json.load(f)
                    results.append(data)
            except Exception as e:
                print(f"Warning: Could not load {filename}: {e}")

    return results

def generate_report(results):
    """Generate markdown report"""
    if not results:
        return "No performance history available yet.\nRun benchmarks to create history."

    # Sort by date
    results.sort(key=lambda x: x.get('date', ''))

    report = ["# Diagon Performance History Report", ""]
    report.append(f"Generated: {datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S')} UTC")
    report.append("")

    # Summary statistics
    p99_values = [float(r.get('p99', 0)) for r in results if r.get('p99')]

    if p99_values:
        report.append("## Summary Statistics")
        report.append("")
        report.append(f"- Total measurements: {len(results)}")
        report.append(f"- P99 Latency:")
        report.append(f"  - Best: {min(p99_values):.3f} ms")
        report.append(f"  - Worst: {max(p99_values):.3f} ms")
        report.append(f"  - Average: {sum(p99_values)/len(p99_values):.3f} ms")
        report.append(f"  - Latest: {p99_values[-1]:.3f} ms")
        report.append("")

    # Trend analysis
    if len(p99_values) >= 2:
        report.append("## Trend Analysis")
        report.append("")

        first = p99_values[0]
        last = p99_values[-1]
        change = ((last - first) / first) * 100

        if change > 10:
            status = "⚠️ Performance degraded"
        elif change < -10:
            status = "✅ Performance improved"
        else:
            status = "➡️ Performance stable"

        report.append(f"- {status}")
        report.append(f"- Change from first measurement: {change:+.1f}%")
        report.append("")

    # Recent history table
    report.append("## Recent History (Last 10 Runs)")
    report.append("")
    report.append("| Date | Commit | P99 (ms) | Docs | Hits | Regression |")
    report.append("|------|--------|----------|------|------|------------|")

    for result in results[-10:]:
        date = result.get('date', 'unknown')[:10]
        commit = result.get('commit', 'unknown')[:7]
        p99 = result.get('p99', 'N/A')
        docs = result.get('docs', 'N/A')
        hits = result.get('hits', 'N/A')
        regression = result.get('regression_vs_baseline', 'N/A')

        if regression != 'N/A':
            regression = f"{float(regression):+.1f}%"

        report.append(f"| {date} | `{commit}` | {p99} | {docs} | {hits} | {regression} |")

    report.append("")

    # Add visualization hint
    report.append("## Visualization")
    report.append("")
    report.append("To visualize trends, use:")
    report.append("```bash")
    report.append("# Plot P99 latency over time")
    report.append("cat performance_history/*.json | jq -r '[.date, .p99] | @csv'")
    report.append("```")

    return "\n".join(report)

def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    history_dir = project_root / "performance_history"
    output_file = project_root / "PERFORMANCE_REPORT.md"

    results = load_history(history_dir)
    report = generate_report(results)

    # Write to file
    with open(output_file, 'w') as f:
        f.write(report)

    print(f"✓ Performance report generated: {output_file}")
    print("")
    print(report)

if __name__ == '__main__':
    main()
