# Diagon Performance Benchmark Guide

## Overview

The `/benchmark_diagon` skill runs pure Diagon performance benchmarks on the Reuters-21578 dataset, focusing on absolute performance and trend tracking (not comparison with Lucene).

## Quick Start

```bash
# Run standard Diagon benchmark
/benchmark_diagon

# Run WAND optimization benchmark
/benchmark_diagon benchmark=wand

# Run all benchmarks
/benchmark_diagon benchmark=both

# Run without baseline comparison
/benchmark_diagon compare_baseline=false
```

## Purpose

### Pure Diagon Performance
- Measure Diagon's **absolute performance** (not relative to Lucene)
- Track performance **trends over time**
- Detect **performance regressions** early
- Validate **optimization improvements**
- Establish **performance baselines**

### Complementary to /benchmark_reuters_lucene

| Aspect | /benchmark_diagon | /benchmark_reuters_lucene |
|--------|-------------------|---------------------------|
| **Focus** | Pure Diagon performance | Diagon vs Lucene comparison |
| **Baseline** | Previous Diagon runs | Apache Lucene |
| **Goal** | Track trends, detect regressions | Meet 3-10x faster target |
| **Report emphasis** | Absolute metrics, trends | Relative speedup vs Lucene |
| **Use case** | Daily performance tracking | Milestone validation |
| **Frequency** | Daily/per-commit | Weekly/per-milestone |

## Available Benchmarks

### 1. Standard Benchmark (`benchmark=reuters`)
**Executable**: `ReutersBenchmark`
**Tests**:
- Indexing: 21,578 documents
- Queries: single-term, Boolean AND, Boolean OR
- Metrics: throughput, latency, index size

### 2. WAND Benchmark (`benchmark=wand`)
**Executable**: `ReutersWANDBenchmark`
**Tests**:
- WAND early termination
- Block-max optimization
- Google Benchmark framework

### 3. Both (`benchmark=both`)
Runs both benchmarks for comprehensive evaluation.

## Dataset

**Reuters-21578**:
- Location: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/`
- Documents: 21,578 news articles from 1987
- Size: ~27 MB
- Standard IR benchmark dataset

## Performance Targets

### Indexing
- **Throughput**: ≥5,000 docs/sec
- **Time**: 2-5 seconds
- **Index size**: 10-15 MB
- **Storage**: 250-700 bytes/doc

### Query Latency (P99)
- **Single-term**: <1ms (1,000 μs)
- **Boolean AND**: <2ms (2,000 μs)
- **Boolean OR (2-term)**: <3ms (3,000 μs)
- **Boolean OR (5-term)**: <5ms (5,000 μs)
- **Boolean OR (10-term)**: <15ms (15,000 μs)
- **Boolean OR (20-term)**: <20ms (20,000 μs)
- **Boolean OR (50-term)**: <30ms (30,000 μs)

### MANDATORY: Percentile Reporting

**All benchmark reports MUST include P50, P90, and P99 percentiles** for every query type. Reporting only P99 is insufficient.

| Percentile | What It Measures | Why It Matters |
|------------|-----------------|----------------|
| P50 | Typical latency | User experience for median queries |
| P90 | Tail latency start | First sign of degradation |
| P99 | Worst-case latency | SLA compliance, outlier detection |

Reports must present results in this format:
```
| Query | P50 (ms) | P90 (ms) | P99 (ms) | Hits |
```

When comparing with Lucene, compare **each percentile separately** (P50 vs P50, P90 vs P90, P99 vs P99).

## Baseline Comparison

### How It Works

1. **First Run**: Establishes baseline
   - Saves metrics to `benchmark_results/diagon_baseline.json`
   - No comparison (nothing to compare against)

2. **Subsequent Runs**: Compares with baseline
   - Loads previous baseline
   - Compares current metrics
   - Detects regressions or improvements
   - Updates baseline if better

### Regression Detection

**Thresholds**:
- ✅ **Improvement**: >10% faster than baseline
- ✅ **Stable**: Within ±10% of baseline
- ⚠️ **Minor regression**: 10-20% slower than baseline
- ❌ **Critical regression**: >20% slower than baseline

**Actions**:
```
✅ Improvement: Celebrate and update baseline
✅ Stable: Good, maintain current performance
⚠️ Minor regression: Investigate, might be acceptable
❌ Critical regression: Must fix before merging
```

## Usage Examples

### Daily Performance Check
```
# Run every day to track trends
/benchmark_diagon

# Check for regressions
cat benchmark_results/diagon_*.md | grep "regression"
```

### Pre-Commit Validation
```bash
# Before committing performance changes
git diff

# Run benchmark
/benchmark_diagon

# If regression detected, investigate before commit
```

### Post-Optimization Validation
```bash
# Before optimization
/benchmark_diagon
# Note baseline metrics

# Make optimization changes
vim src/core/...

# Rebuild and benchmark
/build_diagon target=benchmarks
/benchmark_diagon

# Compare results to validate improvement
```

### Release Validation
```bash
# Before release
/benchmark_diagon benchmark=both

# Review report
cat benchmark_results/diagon_*.md

# Ensure no regressions vs previous release
```

## Report Format

### Uses Same Template
Follows `.claude/BENCHMARK_REPORT_TEMPLATE_V2.md` (same as `/benchmark_reuters_lucene`)

### Report Sections

1. **Executive Summary**
   - Overall result (PASS/PARTIAL/FAIL)
   - Key findings
   - Performance vs baseline
   - Performance vs targets
   - Critical issues

2. **Test Environment**
   - Hardware, software, dataset
   - Build configuration
   - Diagon commit

3. **Indexing Performance**
   - Metrics table
   - Baseline comparison
   - Target comparison

4. **Query Performance**
   - P50/P95/P99 latencies
   - Baseline comparison
   - Target comparison

5. **Performance Analysis**
   - Strengths, improvements, issues
   - Trend analysis

6. **Detailed Comparison**
   - vs Baseline (if available)
   - vs Targets
   - Regression detection

7. **Issues and Concerns**
   - Critical/Important/Minor

8. **Recommendations**
   - Based on findings

9. **Raw Data**
   - Complete output

10. **Reproducibility**
    - Exact commands

### Report Location
```
/home/ubuntu/diagon/benchmark_results/diagon_YYYYMMDD_HHMMSS.md
```

### Baseline Location
```
/home/ubuntu/diagon/benchmark_results/diagon_baseline.json
```

## Baseline Format

```json
{
  "timestamp": "2026-02-09T14:30:22-08:00",
  "commit": "7611493",
  "indexing": {
    "throughput": 6200,
    "time": 3.48,
    "index_size": 13107200
  },
  "queries": {
    "single_term_p99": 450,
    "boolean_and_p99": 1200,
    "boolean_or_p99": 3200
  }
}
```

## Workflow Examples

### Development Workflow
```bash
# 1. Make code changes
vim src/core/search/TermQuery.cpp

# 2. Build
/build_diagon target=benchmarks

# 3. Benchmark
/benchmark_diagon

# 4. Check for regressions
# Review report output

# 5. If improvement, commit
git commit -m "Optimize TermQuery"

# 6. If regression, investigate
# Profile, fix, re-benchmark
```

### CI/CD Integration
```bash
#!/bin/bash
# ci_benchmark.sh

# Build
/build_diagon target=benchmarks

# Benchmark
/benchmark_diagon

# Check for regressions
if grep -q "CRITICAL.*regression" benchmark_results/diagon_*.md; then
    echo "❌ Performance regression detected!"
    exit 1
fi

echo "✅ No performance regressions"
exit 0
```

### Performance Tracking
```bash
# Track trends over time
ls -t benchmark_results/diagon_*.md | head -5

# Extract key metrics
for file in benchmark_results/diagon_20260209_*.md; do
    echo "=== $(basename $file) ==="
    grep -A5 "Throughput" $file
    echo
done

# Plot trends (if you have plotting tools)
./scripts/plot_performance_trends.sh
```

## Troubleshooting

### No Baseline Found
```
ℹ️ No baseline found - this run will become the baseline
```
**Solution**: Normal for first run. Baseline will be saved.

### Regression Detected
```
❌ REGRESSION: Throughput decreased by 800 docs/sec
```
**Solution**:
1. Review recent code changes: `git log -5 --oneline`
2. Profile the code: `perf record ./benchmark`
3. Identify bottleneck
4. Fix and re-benchmark
5. Only commit if no regression

### Dataset Missing
```
❌ Reuters dataset not found!
```
**Solution**: Download dataset (see main benchmark guide)

### Build Fails
```
❌ Build directory not found!
```
**Solution**: Run `/build_diagon target=benchmarks`

## Performance Monitoring

### Metrics to Track

**Indexing**:
- Throughput (docs/sec) - should stay ≥5,000
- Index size (MB) - should stay 10-15
- Time (seconds) - should stay 2-5

**Queries**:
- P50/P90/P99 latency per query type
- Should meet targets (<1ms, <2ms, <5ms, <15ms, <20ms, <30ms)
- OR-20 and OR-50 queries included for high-term-count scaling analysis

**Trends**:
- Improving: ✅ Good
- Stable: ✅ Good
- Regressing: ❌ Must investigate

### Alerting

Set up alerts for:
- ❌ Throughput <4,500 docs/sec (10% below target)
- ❌ Query P99 >1.1x target
- ❌ Index size >18 MB (20% above max)
- ❌ Any critical regression >20%

## Best Practices

### When to Run

**Always**:
- Before committing performance changes
- Before releases
- After optimization work

**Regularly**:
- Daily (for trend tracking)
- After dependency updates
- After system changes

**Optional**:
- After non-performance changes (to detect unintended impact)
- During performance debugging

### Interpreting Results

**Good Signs ✅**:
- All targets met
- No regressions vs baseline
- Performance improving over time
- Stable performance

**Warning Signs ⚠️**:
- Some targets missed (but close)
- Minor regressions (10-20%)
- Increased variability

**Critical Issues ❌**:
- Targets significantly missed
- Major regressions (>20%)
- Crashes or errors
- Incorrect results

### Taking Action

**If Improvement**:
1. Celebrate! 🎉
2. Update baseline
3. Document in commit message
4. Consider blog post if significant

**If Stable**:
1. Good - maintain
2. Continue development
3. Track trends

**If Regression**:
1. Don't commit yet
2. Profile to find cause
3. Fix the issue
4. Re-benchmark
5. Only commit when fixed

## Comparison with Lucene

For Lucene comparison, use `/benchmark_reuters_lucene`:
```bash
# Pure Diagon performance
/benchmark_diagon

# vs Lucene comparison
/benchmark_reuters_lucene
```

Both use same dataset and report template, just different focus.

## Related Documentation

- **Build guide**: `BUILD_SOP.md`
- **Report template**: `.claude/BENCHMARK_REPORT_TEMPLATE_V2.md`
- **Lucene benchmark**: `.claude/skills/BENCHMARK_REUTERS_GUIDE.md`
- **Skills overview**: `.claude/skills/SKILLS_OVERVIEW.md`
- **Project tenets**: `CLAUDE.md`

---

**Last Updated**: 2026-02-13
**Skill Version**: 1.0.0
**Dataset**: Reuters-21578 (21,578 documents)
**Focus**: Pure Diagon performance and trend tracking
