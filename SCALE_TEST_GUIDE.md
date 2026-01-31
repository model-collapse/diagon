# Diagon Scale Testing Guide

Complete guide for running large-scale performance comparison between Diagon and Apache Lucene at 100K, 1M, and 10M document scales.

---

## Overview

This test measures:
- **Query latency**: How fast individual queries execute
- **Query throughput (QPS)**: Queries per second
- **Index build time**: How long indexing takes
- **Index size**: Storage efficiency
- **Scale behavior**: How performance changes with data size

---

## Quick Start

```bash
cd /home/ubuntu/diagon
chmod +x RUN_SCALE_TEST.sh
./RUN_SCALE_TEST.sh
```

**Time estimates**:
- **100K documents**: ~5 minutes
- **1M documents**: ~15 minutes
- **10M documents**: ~45 minutes (uncomment in script)

**Total time**: ~20 minutes for 100K + 1M, ~65 minutes with 10M

---

## What the Test Does

### Phase 1: Build Diagon Indexes

For each scale (100K, 1M, 10M):
1. Generate synthetic documents (100 words each, from 100-word vocabulary)
2. Build Lucene104 index with StreamVByte compression
3. Flush to disk (cached for subsequent runs)
4. Measure:
   - Indexing throughput (docs/sec)
   - Index size (MB)
   - Bytes per document

### Phase 2: Run Diagon Queries

Execute at each scale:
- **TermQuery (common)**: Search for "the"
- **TermQuery (rare)**: Search for "because"
- **BooleanQuery AND**: "the" AND "and"
- **BooleanQuery OR**: "people" OR "time"
- **TopK variation**: Retrieve 10, 100, 1000 results

Measure:
- Latency (microseconds)
- Throughput (QPS)
- Consistency across runs

### Phase 3: Run Lucene Benchmarks

For comparison:
1. Generate matching datasets (same vocabulary, same size)
2. Build Lucene indexes with StandardAnalyzer
3. Run identical queries
4. Measure same metrics

### Phase 4: Compare Results

Generate side-by-side comparison:
- Query latency (Diagon vs Lucene)
- QPS (Diagon vs Lucene)
- Index size comparison
- Speedup ratios
- Winner at each scale

---

## Expected Results

### Hypothesis: Scale Behavior

| Scale | Diagon vs Lucene | Reasoning |
|-------|------------------|-----------|
| **100K** | Lucene faster (0.85-0.95x) | JIT warm, less GC pressure, mature optimizations |
| **1M** | Closer (0.90-1.00x) | Diagon's C++ advantages emerge, Lucene GC impact grows |
| **10M** | Diagon faster (1.05-1.15x) | No GC pauses, better cache utilization, lower memory |

### Projected Performance (Release Mode)

#### 100K Documents

| Query Type | Diagon | Lucene | Speedup |
|------------|--------|--------|---------|
| TermQuery (common) | 90 µs | 85 µs | 0.94x |
| TermQuery (rare) | 88 µs | 82 µs | 0.93x |
| BooleanAND | 135 µs | 125 µs | 0.93x |
| BooleanOR | 168 µs | 155 µs | 0.92x |

**Summary**: Lucene ~5-8% faster at small scale

#### 1M Documents

| Query Type | Diagon | Lucene | Speedup |
|------------|--------|--------|---------|
| TermQuery (common) | 145 µs | 145 µs | 1.00x |
| TermQuery (rare) | 95 µs | 95 µs | 1.00x |
| BooleanAND | 225 µs | 230 µs | 1.02x |
| BooleanOR | 285 µs | 295 µs | 1.04x |

**Summary**: Parity at 1M documents

#### 10M Documents

| Query Type | Diagon | Lucene | Speedup |
|------------|--------|--------|---------|
| TermQuery (common) | 450 µs | 520 µs | 1.16x |
| TermQuery (rare) | 105 µs | 115 µs | 1.10x |
| BooleanAND | 680 µs | 780 µs | 1.15x |
| BooleanOR | 850 µs | 980 µs | 1.15x |

**Summary**: Diagon 10-16% faster at large scale

### Index Size Comparison

| Scale | Diagon | Lucene | Compression |
|-------|--------|--------|-------------|
| 100K | 8.5 MB | 9.2 MB | 0.92x |
| 1M | 85 MB | 92 MB | 0.92x |
| 10M | 850 MB | 920 MB | 0.92x |

**Expected**: Diagon slightly smaller due to StreamVByte compression

---

## Interpreting Results

### Performance Metrics

**Latency (microseconds)**:
- Lower is better
- Watch for scale factor: How much does latency grow?
- Linear growth (2x scale = 2x latency) is ideal
- Sub-linear growth (2x scale = 1.5x latency) is excellent

**QPS (Queries Per Second)**:
- Higher is better
- Shows throughput capacity
- Diagon should maintain or improve relative to Lucene at scale

**Speedup Ratio**:
- \> 1.0x: Diagon faster
- = 1.0x: Parity
- < 1.0x: Lucene faster
- Target: ≥ 0.90x at all scales

### Key Questions

1. **Does Diagon close the gap as scale increases?**
   - YES: C++ advantages (no GC, better cache) emerge at scale
   - NO: Investigate bottlenecks (FST, postings decoding, scoring)

2. **Are Boolean queries competitive?**
   - Important for real-world workloads
   - If slower, consider galloping intersection optimization

3. **Is index size comparable?**
   - StreamVByte should match or beat Lucene's VInt
   - If larger, investigate term dictionary overhead

4. **Does TopK performance degrade?**
   - Should be constant across K=10 to K=1000
   - If degrades, heap implementation issue

---

## Optimization Strategies

### If Diagon is Slower at All Scales (< 0.90x)

**Priority 1: Profile Hot Paths**
```bash
cd /home/ubuntu/diagon/build/benchmarks

# Profile TermQuery
perf record -g ./ScaleComparisonBenchmark --benchmark_filter="Scale_TermQuery/1"
perf report

# Look for:
# - FST traversal overhead
# - Postings decoding bottlenecks
# - BM25 scoring inefficiencies
```

**Priority 2: Optimize FST**
- Add prefetching for FST nodes
- Cache hot FST paths
- Use SIMD for arc lookup

**Priority 3: Optimize Postings Decoding**
- Verify StreamVByte SIMD is active
- Add branch prediction hints
- Prefetch postings blocks

### If Boolean Queries Are Slow (< 0.85x)

**Galloping Intersection**:
```cpp
// Instead of sequential merging:
while (doc1 < doc2) doc1 = nextDoc();
while (doc2 < doc1) doc2 = nextDoc();

// Use binary search (galloping):
doc1 = advance(doc1, doc2);  // Skip many docs at once
```

**WAND Skip Lists**:
- Skip low-scoring documents early
- Requires block-max scores in postings
- 2-3x improvement for selective queries

### If Index Size Is Large (> 1.1x Lucene)

**Check Term Dictionary**:
- FST should be compact
- Verify prefix sharing
- Compare .tim/.tip sizes with Lucene

**Check Postings**:
- StreamVByte should match VInt
- Verify no redundant data
- Compare .doc sizes

---

## Advanced Analysis

### Memory Profiling

**Diagon**:
```bash
valgrind --tool=massif \
    ./ScaleComparisonBenchmark --benchmark_filter="Scale_TermQuery/1"

ms_print massif.out.* | less
```

**Lucene**:
```bash
java -XX:StartFlightRecording=filename=lucene.jfr \
     -jar lucene-benchmark.jar conf/diagon_scale_1M.alg

jfr print --events ObjectAllocationSample lucene.jfr
```

### Cache Analysis

**Diagon**:
```bash
perf stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses \
    ./ScaleComparisonBenchmark --benchmark_filter="Scale_TermQuery/1"
```

Look for:
- L1 cache hit rate > 95%
- L2 cache hit rate > 98%
- L3 cache hit rate > 99%

### Tail Latency (p99, p99.9)

Modify benchmark to collect distribution:
```cpp
std::vector<double> latencies;

for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    TopDocs results = searcher.search(query, 10);
    auto end = std::chrono::high_resolution_clock::now();

    latencies.push_back(std::chrono::duration<double, std::micro>(end - start).count());
}

std::sort(latencies.begin(), latencies.end());
double p50 = latencies[latencies.size() * 0.50];
double p95 = latencies[latencies.size() * 0.95];
double p99 = latencies[latencies.size() * 0.99];
```

**Expected**: Diagon should have better p99/p99.9 (no GC pauses)

---

## Scaling to Real Datasets

### MSMarco (8.8M documents)

The benchmark currently uses synthetic data. To test with real MSMarco:

```cpp
// In ScaleComparisonBenchmark.cpp, add:
const std::vector<DatasetConfig> DATASETS = {
    // ... existing configs ...
    {"MSMarco", 8841822, 150, "/data/msmarco/splade_index"},
};

// TODO: Add MSMarco loader (reads from .jsonl format)
```

**Expected performance**:
- Real documents have variable length (50-300 words)
- More diverse vocabulary (100K+ unique terms)
- Different query selectivity
- More realistic than synthetic tests

### Wikipedia (6M articles)

```bash
# Download Wikipedia dump
wget https://dumps.wikimedia.org/enwiki/latest/enwiki-latest-pages-articles.xml.bz2

# Extract articles (Lucene has built-in extractor)
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
./gradlew getEnWiki
```

---

## Troubleshooting

### Issue: Out of Memory

**Symptom**: Process killed during 10M indexing

**Solution**:
```bash
# Reduce batch size in ScaleComparisonBenchmark.cpp:
const int batchSize = 5000;  // Was 10000

# Or add more swap:
sudo fallocate -l 8G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

### Issue: Indexes Not Cached

**Symptom**: Rebuilding indexes every run

**Check**: `.built` marker file exists:
```bash
ls -la /tmp/diagon_scale_100k/.built
ls -la /tmp/diagon_scale_1m/.built
```

**Fix**: Ensure write permissions to /tmp

### Issue: Lucene Benchmark Hangs

**Symptom**: Java process stuck during indexing

**Solution**:
```bash
# Increase heap size:
java -Xmx16g -Xms16g ...  # Was 8g

# Check for GC thrashing:
java -XX:+PrintGCDetails ...
```

### Issue: Results Don't Match Expected

**Investigate**:
1. Check if Release mode: `strings ScaleComparisonBenchmark | grep -i debug`
2. Verify SIMD enabled: `lscpu | grep -i avx`
3. Check CPU frequency: `cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
   - Should be "performance", not "powersave"

---

## Output Example

```
================================================================
DIAGON SCALE TESTING: 100K, 1M Documents
================================================================

Step 1: Building Diagon in Release mode...
✓ Diagon build complete

Step 2: Running Diagon scale benchmarks...

=== Building 100K index ===
Adding 100000 documents...
  Progress: 10000/100000 (10%)
  Progress: 20000/100000 (20%)
  ...
  Progress: 100000/100000 (100%)
Flushing segment...
✓ Index built in 8.2 seconds
  Throughput: 12195 docs/sec
  Index size: 8.5 MB
  Bytes per doc: 85

Running benchmarks...
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Scale_TermQuery/0             0.090 us        0.090 us     11100000 QPS=11.1M/s docs=100000 index_mb=8.5
BM_Scale_TermQuery/1             0.145 us        0.145 us      6900000 QPS=6.9M/s docs=1000000 index_mb=85

...

✓ Diagon benchmarks complete

====================================================================================================
DIAGON vs APACHE LUCENE: SCALE TESTING COMPARISON
====================================================================================================

====================================================================================================
SCALE: 100K Documents
====================================================================================================

Index Statistics:
  Diagon:
    - Documents: 100,000
    - Index size: 8.5 MB
    - Bytes per doc: 85.0

Search Performance:
                                   Diagon (µs)      Lucene (µs)      Speedup     Winner
  ----------------------------------------------------------------------------------
  TermQuery_Common                        90.00            85.00        0.94x   ✓ Lucene
  TermQuery_Rare                          88.00            82.00        0.93x   ✓ Lucene
  BooleanAND                             135.00           125.00        0.93x   ✓ Lucene
  BooleanOR                              168.00           155.00        0.92x   ✓ Lucene

  QPS (Queries Per Second):
                                   Diagon QPS       Lucene QPS         Ratio     Winner
  ----------------------------------------------------------------------------------
  TermQuery_Common                     11.11M           11.76M        0.94x   ✓ Lucene
  TermQuery_Rare                       11.36M           12.20M        0.93x   ✓ Lucene
  BooleanAND                            7.41M            8.00M        0.93x   ✓ Lucene
  BooleanOR                             5.95M            6.45M        0.92x   ✓ Lucene

====================================================================================================
SCALE: 1M Documents
====================================================================================================

Search Performance:
                                   Diagon (µs)      Lucene (µs)      Speedup     Winner
  ----------------------------------------------------------------------------------
  TermQuery_Common                       145.00           145.00        1.00x      Tie
  TermQuery_Rare                          95.00            95.00        1.00x      Tie
  BooleanAND                             225.00           230.00        1.02x   ✓ Diagon
  BooleanOR                              285.00           295.00        1.04x   ✓ Diagon

====================================================================================================
ANALYSIS
====================================================================================================

100K Documents:
  Average Speedup: 0.93x
  Diagon faster: 0/4 queries
  Lucene faster: 4/4 queries

1M Documents:
  Average Speedup: 1.02x
  Diagon faster: 2/4 queries
  Lucene faster: 0/4 queries

====================================================================================================
```

---

## Next Steps

### After Initial Scale Test

1. **Analyze Bottlenecks**: Profile any slow queries
2. **Optimize Hot Paths**: Focus on FST, postings, scoring
3. **Test Real Data**: MSMarco, Wikipedia for realistic workloads
4. **Add WAND**: Skip low-scoring documents (2-3x improvement)
5. **Multi-threading**: Parallel segment search

### Long-Term Goals

- **Match Lucene at 100K**: Close 5-8% gap with optimizations
- **Beat Lucene at 1M+**: Leverage C++ advantages
- **Better tail latency**: Demonstrate p99 < Lucene (no GC)
- **Lower memory**: Show 20-30% memory savings

---

## References

**Diagon Implementation**:
- ScaleComparisonBenchmark: `/home/ubuntu/diagon/benchmarks/ScaleComparisonBenchmark.cpp`
- Phase 4 Complete: `/home/ubuntu/diagon/PHASE_4_COMPLETE.md`

**Lucene Benchmark**:
- Module: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/`
- By-Task Guide: https://lucene.apache.org/core/9_11_0/benchmark/

**Papers**:
- "Efficient Query Processing with Optimistically Compressed Hash Tables & Strings in the USSR" (Lucene FST)
- "Using Block-Max Indexes for Score-At-A-Time WAND Processing" (WAND optimization)
- "Stream VByte: Faster Byte-Oriented Integer Compression" (StreamVByte)

