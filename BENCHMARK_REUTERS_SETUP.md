# Reuters Benchmark Skill Setup Complete ✅

## Overview

The `/benchmark_reuters_lucene` skill is now available for running standard Reuters-21578 dataset benchmarks to compare Diagon with Apache Lucene.

## Quick Start

```bash
# Run standard benchmark
/benchmark_reuters_lucene

# Run WAND optimization benchmark
/benchmark_reuters_lucene benchmark=wand

# Run all benchmarks
/benchmark_reuters_lucene benchmark=both

# Quick re-run (skip build)
/benchmark_reuters_lucene build=false
```

## What Was Created

```
.claude/skills/
├── benchmark_reuters_lucene.json   # New: Reuters vs Lucene benchmark skill
├── BENCHMARK_REUTERS_GUIDE.md      # New: Comprehensive guide
├── README.md                       # Updated: Added benchmark skill
└── SKILLS_OVERVIEW.md              # Updated: Added benchmark section

Documentation:
└── BENCHMARK_REUTERS_SETUP.md      # This file
```

## About the Benchmark

### Reuters-21578 Dataset
- **Documents**: 21,578 news articles from 1987
- **Location**: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/`
- **Format**: Plain text files (one per document)
- **Purpose**: Standard IR benchmark used by Apache Lucene
- **Use**: Enables direct performance comparison with Lucene

### Available Benchmarks

1. **Standard Benchmark** (`benchmark=reuters`)
   - Indexing performance (21,578 documents)
   - Query performance (single-term, AND, OR)
   - Reports: throughput, latency, index size
   - Executable: `ReutersBenchmark`

2. **WAND Benchmark** (`benchmark=wand`)
   - WAND (Weak AND) optimization testing
   - Block-max scoring evaluation
   - Google Benchmark framework
   - Executable: `ReutersWANDBenchmark`

3. **Both** (`benchmark=both`)
   - Runs all benchmarks sequentially
   - Comprehensive performance evaluation

## Skill Features

✅ **Auto-build**: Builds benchmarks before running (optional)
✅ **Clean index**: Removes old index for fresh results
✅ **Save results**: Automatic timestamped result files
✅ **Dataset verification**: Checks Reuters dataset exists
✅ **Performance context**: Reports vs Lucene baseline
✅ **Error handling**: Helpful troubleshooting messages

## Usage Examples

### Basic Usage
```
# Run with all defaults
/benchmark_reuters_lucene
```

### Custom Configuration
```
# WAND optimization only
/benchmark_reuters_lucene benchmark=wand

# Full evaluation
/benchmark_reuters_lucene benchmark=both

# Skip build step
/benchmark_reuters_lucene build=false

# Don't save results
/benchmark_reuters_lucene save_results=false
```

### Typical Workflow
```bash
# 1. Build benchmarks
/build_diagon target=benchmarks

# 2. Run Reuters benchmark
/benchmark_reuters_lucene

# 3. Check results
cat /home/ubuntu/diagon/benchmark_results/reuters_*.txt
```

## Expected Output

### Standard Benchmark
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

  Boolean AND: 'oil AND price'
    Latency (P99): 1200 μs
    Hits: 654

  Boolean OR 5-term: 'oil OR trade...'
    Latency (P99): 3200 μs
    Hits: 8945
```

### WAND Benchmark
```
------------------------------------------------------------
Benchmark                  Time             CPU   Iterations
------------------------------------------------------------
BM_WAND_Reuters/10       2.5 ms          2.5 ms          280
BM_WAND_Reuters/100      4.8 ms          4.8 ms          146
```

## Performance Targets

### Indexing
- **Throughput**: ≥5,000 docs/sec (competitive with Lucene)
- **Index size**: 10-15 MB (similar to Lucene)
- **Time**: 2-4 seconds (fast enough)

### Query Latency (vs Lucene)
- **Single term**: 3-5x faster
- **Boolean AND**: 3-5x faster
- **Boolean OR**: 3-5x faster
- **Overall goal**: 3-10x faster than Lucene

## Results Location

Results are automatically saved to:
```
/home/ubuntu/diagon/benchmark_results/reuters_YYYYMMDD_HHMMSS.txt
```

Example:
```
/home/ubuntu/diagon/benchmark_results/reuters_20260209_143022.txt
```

## Prerequisites

### 1. Reuters Dataset
The dataset must be downloaded:
```bash
# Check if dataset exists
ls /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/*.txt | wc -l
# Should show: 21578

# If missing, download:
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant get-reuters
```

### 2. Build Environment
Benchmarks must be built:
```bash
# Using skill
/build_diagon target=benchmarks

# Or manually
cd /home/ubuntu/diagon/build
make ReutersBenchmark ReutersWANDBenchmark -j8
```

## Troubleshooting

### Dataset Not Found
```
❌ Reuters dataset not found!
```

**Fix**: Download the dataset
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant get-reuters
```

### Benchmark Executable Missing
```
❌ ReutersBenchmark not found!
```

**Fix**: Build benchmarks
```bash
/build_diagon target=benchmarks
```

### ICU Linking Error
```
error while loading shared libraries: libicuuc.so
```

**Fix**: Rebuild with clean build
```bash
/build_diagon target=benchmarks clean=true
```

### Benchmark Crashes
1. Verify build: `/build_diagon target=benchmarks`
2. Check ICU: `ldd build/benchmarks/ReutersBenchmark | grep icu`
3. Check dataset: `ls /home/.../reuters-out/*.txt | wc -l`
4. Check disk space: `df -h /tmp`

## Comparison with Lucene

To compare with Lucene's results:

```bash
# Run Lucene's Reuters benchmark
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant run-task -Dtask.alg=conf/reuters.alg

# Run Diagon's Reuters benchmark
/benchmark_reuters_lucene

# Compare results
# Focus on: indexing throughput, query latency, index size
```

## Integration with Workflow

### Development Cycle
```bash
# 1. Make code changes
vim src/core/...

# 2. Build
/build_diagon target=benchmarks

# 3. Benchmark
/benchmark_reuters_lucene

# 4. Compare with baseline
diff benchmark_results/reuters_*.txt

# 5. Iterate
```

### CI/CD Integration
```bash
#!/bin/bash
# Automated benchmark check

/build_diagon target=benchmarks
/benchmark_reuters_lucene save_results=true

# Check for performance regression
THROUGHPUT=$(grep "Throughput:" results.txt | awk '{print $2}')
if [ "$THROUGHPUT" -lt 4000 ]; then
    echo "❌ Performance regression detected!"
    exit 1
fi
```

## Performance Philosophy

From CLAUDE.md tenets:
- **Be Self-discipline**: Only report correctly built artifacts
- **Be Honest**: Report actual numbers, not predictions
- **Be Humble and Straight**: Don't hide performance issues
- **Insist Highest Standard**: Target 3-10x faster than Lucene
- **No excuse for falling behind**: If slower, investigate and fix

## Next Steps

1. **Try the skill**: `/benchmark_reuters_lucene`
2. **Run all tests**: `/benchmark_reuters_lucene benchmark=both`
3. **Compare with Lucene**: Run both and compare results
4. **Track progress**: Save results and monitor improvements
5. **Read guide**: See `.claude/skills/BENCHMARK_REUTERS_GUIDE.md`

## Documentation

- **Quick reference**: `.claude/skills/README.md`
- **Detailed guide**: `.claude/skills/BENCHMARK_REUTERS_GUIDE.md`
- **Complete overview**: `.claude/skills/SKILLS_OVERVIEW.md`
- **Build SOP**: `BUILD_SOP.md`
- **Project guide**: `CLAUDE.md`

## Available Skills Summary

| Skill | Purpose | Quick Command |
|-------|---------|---------------|
| `/build_diagon` | Build project | `/build_diagon` |
| `/build_lucene` | Build project (alt) | `/build_lucene` |
| `/benchmark_reuters_lucene` | Reuters benchmark | `/benchmark_reuters_lucene` |

---

**Status**: ✅ Skill ready
**Dataset**: Reuters-21578 (21,578 documents)
**Target**: 3-10x faster than Apache Lucene
**Date**: 2026-02-09

Try it now:
```
/benchmark_reuters_lucene
```
