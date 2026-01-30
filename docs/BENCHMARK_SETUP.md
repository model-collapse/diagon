# Diagon vs Lucene Benchmark Setup Guide

This guide walks you through setting up the complete benchmark infrastructure for comparing Diagon with Apache Lucene side-by-side.

## Overview

The benchmark infrastructure consists of:
- **Diagon benchmarks**: Google Benchmark-based tests in `/home/ubuntu/diagon/benchmarks/`
- **Lucene benchmarks**: By-Task algorithm files in `/home/ubuntu/opensearch_warmroom/lucene/`
- **Analysis scripts**: Python scripts to aggregate and compare results
- **Datasets**: Reuters-21578 and Wikipedia for realistic testing

## Phase 1: Setup (Week 1)

### Step 1.1: Configure Benchmark Machine

**Install profiling tools:**
```bash
sudo apt-get update
sudo apt-get install -y linux-tools-generic valgrind sysstat
```

**Disable CPU frequency scaling (for consistent results):**
```bash
# Check current governor
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Set to performance mode (requires root)
sudo cpupower frequency-set --governor performance

# Verify
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
# Expected: "performance" for all CPUs
```

**Disable swap (optional, for memory benchmarks):**
```bash
sudo swapoff -a
```

**Verify perf works:**
```bash
perf stat echo "test"
# Should show timing statistics
```

### Step 1.2: Prepare Datasets

**Download Reuters-21578 dataset:**
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
./gradlew getReuters

# Verify extraction
ls -lh work/reuters21578/
# Expected: ~12MB of SGML files (21,578 documents)
```

**Download Wikipedia dataset (optional, for large-scale tests):**
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
./gradlew getEnWiki

# Extract 1M articles (this takes time)
java -cp build/libs/lucene-benchmark.jar \
    org.apache.lucene.benchmark.byTask.feeds.WikipediaExtractor \
    work/enwiki-latest-pages-articles.xml.bz2 \
    work/enwiki-1m-docs.txt 1000000
```

**Convert to Diagon format (for real datasets):**
```bash
# The LuceneDatasetAdapter will read Lucene's LineDocSource format directly
# No conversion needed for synthetic data (generated on-the-fly)
```

### Step 1.3: Build Diagon Benchmarks

```bash
cd /home/ubuntu/diagon

# Configure with Release mode
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DDIAGON_BUILD_BENCHMARKS=ON

# Build
cmake --build build -j$(nproc)

# Verify benchmark binary exists
ls -lh build/benchmarks/LuceneCompatBenchmark
```

### Step 1.4: Build Lucene Benchmarks

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Build benchmark JAR
./gradlew assemble

# Verify JAR exists
ls -lh build/libs/lucene-benchmark.jar
```

## Phase 2: Run Baseline Comparison (Week 2)

### Step 2.1: Run Diagon Benchmarks

**Single run (for testing):**
```bash
cd /home/ubuntu/diagon/build

# Clear OS page cache (requires sudo)
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

# Run benchmarks
taskset -c 0-3 ./benchmarks/LuceneCompatBenchmark
```

**Multiple runs (for statistical significance):**
```bash
cd /home/ubuntu/diagon/build

# Run 5 iterations
for i in {1..5}; do
    echo "Run $i"
    sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
    taskset -c 0-3 ./benchmarks/LuceneCompatBenchmark \
        --benchmark_out=diagon_results_run${i}.json \
        --benchmark_out_format=json
    sleep 30  # Cool down between runs
done

# Aggregate results
python3 ../scripts/aggregate_results.py diagon_results_*.json > diagon_baseline.csv

# View results
cat diagon_baseline.csv
```

### Step 2.2: Run Lucene Benchmarks

**Single run (for testing):**
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Clear cache
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

# Run with JVM configuration
taskset -c 0-3 java -Xmx4g -Xms4g -XX:+AlwaysPreTouch \
    -XX:+UseG1GC -XX:MaxGCPauseMillis=100 \
    -jar build/libs/lucene-benchmark.jar \
    conf/diagon_baseline.alg
```

**Multiple runs:**
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

for i in {1..5}; do
    echo "Run $i"
    sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

    taskset -c 0-3 java -Xmx4g -Xms4g -XX:+AlwaysPreTouch \
        -XX:+UseG1GC -XX:MaxGCPauseMillis=100 \
        -jar build/libs/lucene-benchmark.jar \
        conf/diagon_baseline.alg > lucene_results_run${i}.txt

    sleep 30
done

# Parse and aggregate results
python3 scripts/parse_lucene_results.py lucene_results_*.txt > lucene_baseline.csv

# View results
cat lucene_baseline.csv
```

### Step 2.3: Generate Comparison Report

```bash
cd /home/ubuntu/diagon

# Compare results
python3 scripts/compare_benchmarks.py \
    build/diagon_baseline.csv \
    /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/lucene_baseline.csv \
    > BENCHMARK_COMPARISON.md

# View report
cat BENCHMARK_COMPARISON.md
```

**Expected output:**
- Summary statistics (how many benchmarks, Diagon faster vs Lucene faster)
- Detailed performance gaps table
- Identified bottlenecks (>20% slower)
- Next steps recommendations

## Troubleshooting

### Diagon Benchmarks

**Error: "File not found: diagon/document/Document.h"**
- Solution: Make sure you built with `-DDIAGON_BUILD_BENCHMARKS=ON`
- Solution: Check CMakeLists.txt includes the core library

**Error: "Cannot open index directory"**
- Solution: Check INDEX_DIR exists or can be created (`/tmp/diagon_benchmark_index`)
- Solution: Run `rm -rf /tmp/diagon_benchmark_index` to clean old indexes

### Lucene Benchmarks

**Error: "Class not found: ReutersContentSource"**
- Solution: Make sure you ran `./gradlew assemble` first
- Solution: Check `lucene-benchmark.jar` exists in `build/libs/`

**Error: "Reuters dataset not found"**
- Solution: Run `./gradlew getReuters` to download dataset
- Solution: Check `work/reuters21578/` directory exists

### Analysis Scripts

**Error: "No benchmarks found"**
- Solution: Check JSON output contains `"benchmarks"` key
- Solution: Verify benchmark ran successfully (non-empty output)

**Error: "No matching benchmarks"**
- Solution: Benchmark names must match between Diagon and Lucene
- Solution: Check naming conventions in scripts

## Next Steps

After completing Phase 1 and 2, proceed to:

1. **Phase 3: Deep Profiling** - Use `perf`, Valgrind to identify bottlenecks
2. **Phase 4: Optimization** - Fix identified bottlenecks, re-run benchmarks
3. **Phase 5: Monitoring** - Set up CI/CD for continuous tracking

See `/home/ubuntu/.claude/plans/happy-inventing-peach.md` for the full plan.

## File Structure

```
/home/ubuntu/diagon/
├── benchmarks/
│   ├── LuceneCompatBenchmark.cpp      # Main benchmark suite
│   └── dataset/
│       ├── LuceneDatasetAdapter.h     # Read Lucene datasets
│       └── SyntheticGenerator.h       # Generate synthetic docs
├── scripts/
│   ├── aggregate_results.py           # Aggregate multiple runs
│   └── compare_benchmarks.py          # Compare Diagon vs Lucene
└── docs/
    └── BENCHMARK_SETUP.md             # This file

/home/ubuntu/opensearch_warmroom/lucene/
└── lucene/benchmark/
    ├── conf/
    │   └── diagon_baseline.alg        # Lucene benchmark config
    └── scripts/
        └── parse_lucene_results.py    # Parse Lucene output
```

## Performance Tips

1. **Consistent Environment**: Always clear page cache, use `taskset` for CPU pinning
2. **Multiple Runs**: Run 5+ iterations, take median to reduce variance
3. **Cool Down**: Wait 30 seconds between runs to avoid thermal throttling
4. **Disable Background**: Close unnecessary apps, disable cron jobs during benchmarking
5. **Monitor**: Use `htop`, `iostat` to ensure system is stable

## Questions?

For issues or questions about the benchmark infrastructure:
- Check the full plan: `/home/ubuntu/.claude/plans/happy-inventing-peach.md`
- Review existing benchmark results: `/home/ubuntu/diagon/*.md` files
- Run benchmarks in verbose mode: `./LuceneCompatBenchmark --benchmark_list_tests`
