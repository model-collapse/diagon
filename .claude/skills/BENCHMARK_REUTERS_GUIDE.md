# Reuters-21578 Benchmark Guide

## Overview

The `/benchmark_reuters_lucene` skill runs the standard Reuters-21578 dataset benchmark used by Apache Lucene for performance evaluation.

## Quick Start

```bash
# Run standard benchmark
/benchmark_reuters_lucene

# Run WAND optimization benchmark
/benchmark_reuters_lucene benchmark=wand

# Run all benchmarks
/benchmark_reuters_lucene benchmark=both

# Skip build step (use existing binary)
/benchmark_reuters_lucene build=false
```

## Dataset Information

### Reuters-21578 Collection
- **Source**: Reuters newswire from 1987
- **Documents**: 21,578 news articles
- **Location**: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/`
- **Format**: Plain text files (one per document)
- **Size**: ~27 MB total
- **Fields**: date, title, body

### Why Reuters?
- Industry standard benchmark for information retrieval
- Used by Apache Lucene for performance testing
- Enables direct comparison with published results
- Real-world news article distribution
- Moderate size (good for quick iterations)

## Available Benchmarks

### 1. Standard Reuters Benchmark (`benchmark=reuters`)

**Executable**: `ReutersBenchmark`
**Source**: `benchmarks/reuters_benchmark.cpp`

**Tests**:
1. **Indexing Performance**
   - Index all 21,578 documents
   - Measure throughput (docs/sec)
   - Calculate index size
   - Storage efficiency (bytes/doc)

2. **Query Performance**
   - Single-term queries: "dollar", "oil", "trade"
   - Boolean AND: "oil AND price"
   - Boolean OR (2-term): "trade OR export"
   - Boolean OR (5-term): "oil OR trade OR market OR price OR dollar"
   - Measures P99 latency and hit counts

**Output**:
```
=========================================
Reuters-21578 Benchmark Results
=========================================

Indexing Performance:
  Documents: 21578
  Time: 2.5 seconds
  Throughput: 8631 docs/sec
  Index size: 12 MB
  Storage: 584 bytes/doc

Query Performance:
  Single term: 'dollar'
    Latency (P99): 450 μs
    Hits: 2847

  Boolean OR 5-term: 'oil OR trade...'
    Latency (P99): 3200 μs
    Hits: 8945
```

### 2. WAND Benchmark (`benchmark=wand`)

**Executable**: `ReutersWANDBenchmark`
**Source**: `benchmarks/ReutersWANDBenchmark.cpp`

**Tests**:
- WAND (Weak AND) early termination optimization
- Block-max WAND with impact scoring
- Uses Google Benchmark framework
- Detailed performance profiling

**Output**:
```
------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
------------------------------------------------------------
BM_WAND_Reuters/10       2.5 ms          2.5 ms          280
BM_WAND_Reuters/100      4.8 ms          4.8 ms          146
```

### 3. Both (`benchmark=both`)

Runs both benchmarks sequentially for comprehensive evaluation.

## Usage Examples

### Quick Benchmark Run
```
/benchmark_reuters_lucene
```
Builds and runs standard benchmark with all defaults.

### WAND-Only Benchmark
```
/benchmark_reuters_lucene benchmark=wand
```
Tests WAND optimization specifically.

### Full Evaluation
```
/benchmark_reuters_lucene benchmark=both
```
Runs all benchmarks for comprehensive results.

### Quick Re-run (No Rebuild)
```
/benchmark_reuters_lucene build=false
```
Uses existing binaries (faster if already built).

### Custom Configuration
```
/benchmark_reuters_lucene benchmark=both build=true clean_index=true save_results=true
```
Full control over all parameters.

## Results Location

Results are saved to:
```
/home/ubuntu/diagon/benchmark_results/reuters_YYYYMMDD_HHMMSS.txt
```

Example:
```
/home/ubuntu/diagon/benchmark_results/reuters_20260209_143022.txt
```

## Performance Targets

### Indexing
| Metric | Lucene | Diagon Target | Notes |
|--------|--------|---------------|-------|
| Throughput | 5-10K docs/sec | ≥5K docs/sec | Competitive |
| Index size | 10-15 MB | 10-15 MB | Similar compression |
| Time | 2-4 seconds | 2-4 seconds | Fast enough |

### Query Latency (P99)
| Query Type | Lucene | Diagon Target | Speedup Goal |
|------------|--------|---------------|--------------|
| Single term | 500-1000 μs | 100-200 μs | 3-5x faster |
| Boolean AND | 1-2 ms | 200-500 μs | 3-5x faster |
| Boolean OR (2) | 2-4 ms | 500-1000 μs | 3-4x faster |
| Boolean OR (5) | 5-10 ms | 1-2 ms | 3-5x faster |

**Key Goal**: 3-10x faster than Lucene on search queries.

## Interpreting Results

### Good Results ✅
- Indexing throughput > 5,000 docs/sec
- Query latency significantly lower than Lucene
- Index size comparable to Lucene
- No crashes or errors
- All queries return correct hit counts

### Concerning Results ⚠️
- Indexing slower than 3,000 docs/sec
- Query latency similar to or slower than Lucene
- Index size significantly larger than Lucene
- Inconsistent results across runs
- Memory issues or crashes

### Action Required ❌
- Query latency slower than Lucene
- Crashes or errors during benchmark
- Incorrect query results
- Extremely large index size (>20 MB)

## Troubleshooting

### Dataset Not Found
```
❌ Reuters dataset not found!
```

**Solution**:
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant get-reuters

# Verify
ls work/reuters-out/*.txt | wc -l
# Should show: 21578
```

### Benchmark Executable Missing
```
❌ ReutersBenchmark not found!
```

**Solution**:
```bash
/build_diagon target=benchmarks
# Or manually:
cd /home/ubuntu/diagon/build
make ReutersBenchmark -j8
```

### ICU Linking Error
```
error while loading shared libraries: libicuuc.so
```

**Solution**:
```bash
# Verify ICU linking
ldd /home/ubuntu/diagon/build/benchmarks/ReutersBenchmark | grep icu

# If missing, rebuild
/build_diagon target=benchmarks clean=true
```

### Benchmark Crashes
1. Check build is correct: `/build_diagon target=benchmarks`
2. Verify dataset integrity: `ls /home/.../reuters-out/*.txt | wc -l`
3. Check disk space: `df -h /tmp`
4. Review error logs for specific issues

## Comparison with Lucene

### Running Lucene Benchmark

For direct comparison, run Lucene's Reuters benchmark:

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant run-task -Dtask.alg=conf/reuters.alg
```

### Key Metrics to Compare

1. **Indexing Throughput**
   - Diagon vs Lucene docs/sec
   - Should be competitive (±20%)

2. **Query Latency**
   - Target: 3-10x faster
   - Compare P99 latencies directly

3. **Index Size**
   - Should be similar (within 10-20%)
   - Compression effectiveness

4. **Memory Usage**
   - Peak memory during indexing
   - Query-time memory overhead

## Best Practices

### Before Benchmarking
1. ✅ Build with Release mode (no LTO)
2. ✅ Close unnecessary applications
3. ✅ Clean old indexes
4. ✅ Ensure dataset is available
5. ✅ Have sufficient disk space (/tmp)

### During Benchmarking
1. ✅ Don't run other CPU-intensive tasks
2. ✅ Monitor for errors/warnings
3. ✅ Save results to files
4. ✅ Note any anomalies

### After Benchmarking
1. ✅ Compare with previous runs
2. ✅ Compare with Lucene results
3. ✅ Document any performance regressions
4. ✅ Investigate slower-than-expected results
5. ✅ Share results for team review

## Performance Tracking

### Save Results History
```bash
# Results automatically saved to:
/home/ubuntu/diagon/benchmark_results/

# Compare with previous run:
diff benchmark_results/reuters_20260209_120000.txt \
     benchmark_results/reuters_20260209_150000.txt
```

### Create Comparison Report
```bash
# Extract key metrics
grep "Throughput:" benchmark_results/reuters_*.txt
grep "Latency (P99):" benchmark_results/reuters_*.txt
```

## Integration with CI/CD

The Reuters benchmark can be automated:

```bash
#!/bin/bash
# ci_reuters_benchmark.sh

# Build and run benchmark
/build_diagon target=benchmarks
/benchmark_reuters_lucene benchmark=both save_results=true

# Check for regressions
THROUGHPUT=$(grep "Throughput:" results.txt | awk '{print $2}')
if [ "$THROUGHPUT" -lt 4000 ]; then
    echo "Performance regression detected!"
    exit 1
fi
```

## Related Documentation

- **Build SOP**: `BUILD_SOP.md` - How to build correctly
- **Skills Overview**: `.claude/skills/SKILLS_OVERVIEW.md` - All available skills
- **Project Guide**: `CLAUDE.md` - Project tenets and goals
- **Benchmark Results**: `benchmark_results/` - Historical results

---

**Last Updated**: 2026-02-09
**Dataset**: Reuters-21578 (21,578 documents)
**Target**: 3-10x faster than Apache Lucene
