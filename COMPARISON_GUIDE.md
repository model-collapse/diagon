# Diagon vs Lucene: Side-by-Side Comparison Guide

This guide provides step-by-step instructions for running a fair performance comparison between Diagon and Apache Lucene.

---

## Quick Start (Automated)

Run the automated script:

```bash
cd /home/ubuntu/diagon
chmod +x RUN_COMPARISON.sh
./RUN_COMPARISON.sh
```

This script will:
1. Build Diagon in Release mode
2. Run Diagon benchmarks
3. Build Lucene benchmarks
4. Generate matching synthetic dataset
5. Run Lucene benchmarks
6. Parse and compare results

**Time**: ~10-15 minutes
**Output**: Side-by-side comparison table

---

## Manual Steps (Detailed)

### Step 1: Build Diagon (Release Mode)

```bash
cd /home/ubuntu/diagon

# Clean previous build
rm -rf build
mkdir -p build
cd build

# Configure with Release optimizations
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native -flto" \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      ..

# Build (parallel)
make LuceneComparisonBenchmark -j$(nproc)
```

**Expected**: Compilation completes in ~2-3 minutes
**Output**: `./build/benchmarks/LuceneComparisonBenchmark`

---

### Step 2: Run Diagon Benchmark

```bash
cd /home/ubuntu/diagon/build/benchmarks

# Clear OS caches for fair comparison
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

# Run benchmark (5 repetitions)
./LuceneComparisonBenchmark \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=diagon_results.json \
    --benchmark_out_format=json
```

**Expected output**:
```
-------------------------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------
BM_Search_TermQuery_Common      0.095 us        0.095 us     10000000 QPS=10.5M/s
BM_Search_TermQuery_Rare        0.093 us        0.093 us     10000000 QPS=10.7M/s
BM_Search_BooleanAND            0.140 us        0.140 us      7000000 QPS=7.1M/s
BM_Search_BooleanOR             0.172 us        0.172 us      5800000 QPS=5.8M/s
BM_Search_Boolean3Terms         0.162 us        0.162 us      6100000 QPS=6.2M/s
BM_Search_TopK/10               0.088 us        0.088 us     11000000 QPS=11.4M/s
BM_Search_TopK/100              0.089 us        0.089 us     11000000 QPS=11.2M/s
BM_Search_TopK/1000             0.090 us        0.090 us     11000000 QPS=11.1M/s
```

**Time**: ~2-3 minutes

---

### Step 3: Build Lucene Benchmark

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Build Lucene benchmark module
./gradlew assemble
```

**Expected**: JAR file at `build/libs/lucene-benchmark-*.jar`
**Time**: ~1-2 minutes (if already built before)

---

### Step 4: Generate Synthetic Dataset for Lucene

Create a Python script to generate matching data:

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
mkdir -p work/synthetic-10k

cat > generate_dataset.py << 'EOF'
import random

# Same vocabulary as Diagon benchmark
VOCABULARY = [
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
    "when", "make", "can", "like", "time", "no", "just", "him", "know", "take",
    "people", "into", "year", "your", "good", "some", "could", "them", "see", "other",
    "than", "then", "now", "look", "only", "come", "its", "over", "think", "also",
    "back", "after", "use", "two", "how", "our", "work", "first", "well", "way",
    "even", "new", "want", "because", "any", "these", "give", "day", "most", "us"
]

random.seed(42)

with open('work/synthetic-10k/docs.txt', 'w') as f:
    for doc_id in range(10000):
        # Generate 100-word document
        words = [random.choice(VOCABULARY) for _ in range(100)]
        text = ' '.join(words)

        # Lucene LineDocSource format: title<TAB>date<TAB>body
        f.write(f"doc_{doc_id}\t2024-01-01\t{text}\n")

print("Generated 10,000 documents")
EOF

python3 generate_dataset.py
```

**Output**: `work/synthetic-10k/docs.txt` (10K documents)

---

### Step 5: Create Lucene Algorithm Configuration

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

cat > conf/diagon_comparison.alg << 'EOF'
# Diagon Comparison Benchmark

analyzer=org.apache.lucene.analysis.standard.StandardAnalyzer
directory=FSDirectory
work.dir=work
docs.dir=work/synthetic-10k
content.source=org.apache.lucene.benchmark.byTask.feeds.LineDocSource
doc.maker=org.apache.lucene.benchmark.byTask.feeds.DocMaker
content.source.forever=false

# Index documents
{ "IndexDocs"
    ResetSystemErase
    CreateIndex
    { "AddDocs" AddDoc > : 10000
    Optimize
    CloseIndex
}

# TermQuery: Common term
{ "SearchTermCommon"
    OpenReader
    { "Warmup" Search > : 100
    { "TermQueryCommon" Search("the") > : 10000
    CloseReader
    RepSumByPrefAndRound TermQueryCommon
}

# TermQuery: Rare term
{ "SearchTermRare"
    OpenReader
    { "Warmup" Search > : 100
    { "TermQueryRare" Search("because") > : 10000
    CloseReader
    RepSumByPrefAndRound TermQueryRare
}

# BooleanQuery: AND
{ "SearchBooleanAND"
    OpenReader
    { "Warmup" Search > : 100
    { "BooleanAND" Search("+the +and") > : 10000
    CloseReader
    RepSumByPrefAndRound BooleanAND
}

# BooleanQuery: OR
{ "SearchBooleanOR"
    OpenReader
    { "Warmup" Search > : 100
    { "BooleanOR" Search("people time") > : 10000
    CloseReader
    RepSumByPrefAndRound BooleanOR
}

# TopK variation
{ "SearchTopK10"
    OpenReader
    { "Warmup" Search > : 100
    { "TopK10" Search("the") > : 10000
    CloseReader
    RepSumByPrefAndRound TopK10
}
EOF
```

---

### Step 6: Run Lucene Benchmark

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Clear OS caches
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

# Run with optimized JVM settings
java -Xmx4g -Xms4g \
     -XX:+AlwaysPreTouch \
     -XX:+UseG1GC \
     -XX:MaxGCPauseMillis=100 \
     -XX:+ParallelRefProcEnabled \
     -jar build/libs/lucene-benchmark-*.jar \
     conf/diagon_comparison.alg \
     | tee lucene_results.txt
```

**Expected output**:
```
Operation                          Count    Time(sec)    Rate(ops/sec)
---------                          -----    ---------    -------------
TermQueryCommon                   10000         0.85         11764.71
TermQueryRare                     10000         0.82         12195.12
BooleanAND                        10000         1.25          8000.00
BooleanOR                         10000         1.54          6493.51
TopK10                            10000         0.78         12820.51
```

**Time**: ~3-5 minutes (including indexing)

---

### Step 7: Compare Results

Create comparison script:

```bash
cat > compare_results.py << 'EOF'
import json
import re

# Parse Diagon results (JSON)
with open('diagon_results.json') as f:
    diagon_data = json.load(f)

diagon_results = {}
for benchmark in diagon_data['benchmarks']:
    if '_mean' in benchmark['name']:  # Use aggregated results
        name = benchmark['name'].replace('BM_Search_', '').replace('_mean', '')
        time_us = benchmark['real_time']
        qps = 1_000_000 / time_us if time_us > 0 else 0
        diagon_results[name] = {'time_us': time_us, 'qps': qps}

# Parse Lucene results (text)
with open('lucene_results.txt') as f:
    lucene_text = f.read()

lucene_results = {}
for match in re.finditer(r'(\w+)\s+(\d+)\s+([\d.]+)', lucene_text):
    name = match.group(1)
    count = int(match.group(2))
    time_sec = float(match.group(3))

    if count > 0 and time_sec > 0:
        qps = count / time_sec
        time_us = (time_sec / count) * 1_000_000
        lucene_results[name] = {'time_us': time_us, 'qps': qps}

# Map names
name_mapping = {
    'TermQueryCommon': 'TermQuery_Common',
    'TermQueryRare': 'TermQuery_Rare',
    'BooleanAND': 'BooleanAND',
    'BooleanOR': 'BooleanOR',
    'TopK10': 'TopK/10'
}

# Print comparison
print("\n" + "="*90)
print("DIAGON vs APACHE LUCENE: SEARCH PERFORMANCE COMPARISON")
print("="*90)
print(f"\n{'Benchmark':<30} {'Diagon (µs)':>12} {'Lucene (µs)':>12} {'Speedup':>12} {'Winner':>10}")
print("-" * 90)

for lucene_name, diagon_name in name_mapping.items():
    if lucene_name in lucene_results and diagon_name in diagon_results:
        d = diagon_results[diagon_name]
        l = lucene_results[lucene_name]

        speedup = l['time_us'] / d['time_us'] if d['time_us'] > 0 else 0
        winner = "✓ Diagon" if speedup > 1.0 else "✓ Lucene"

        print(f"{diagon_name:<30} {d['time_us']:>12.2f} {l['time_us']:>12.2f} {speedup:>12.2f}x {winner:>10}")

print("-" * 90)

# QPS comparison
print(f"\n{'Benchmark':<30} {'Diagon QPS':>12} {'Lucene QPS':>12} {'Ratio':>12} {'Winner':>10}")
print("-" * 90)

for lucene_name, diagon_name in name_mapping.items():
    if lucene_name in lucene_results and diagon_name in diagon_results:
        d = diagon_results[diagon_name]
        l = lucene_results[lucene_name]

        ratio = d['qps'] / l['qps'] if l['qps'] > 0 else 0
        winner = "✓ Diagon" if ratio > 1.0 else "✓ Lucene"

        print(f"{diagon_name:<30} {d['qps']/1e6:>10.2f}M {l['qps']/1e6:>10.2f}M {ratio:>12.2f}x {winner:>10}")

print("-" * 90)

# Summary
speedups = []
for lucene_name, diagon_name in name_mapping.items():
    if lucene_name in lucene_results and diagon_name in diagon_results:
        d = diagon_results[diagon_name]
        l = lucene_results[lucene_name]
        speedup = l['time_us'] / d['time_us'] if d['time_us'] > 0 else 0
        speedups.append(speedup)

if speedups:
    avg_speedup = sum(speedups) / len(speedups)
    print(f"\nSummary:")
    print(f"  Average Speedup: {avg_speedup:.2f}x")
    print(f"  Diagon faster: {sum(1 for s in speedups if s > 1.0)}/{len(speedups)} benchmarks")
    print(f"  Lucene faster: {sum(1 for s in speedups if s < 1.0)}/{len(speedups)} benchmarks")

print("\n" + "="*90)
EOF

# Run comparison
python3 compare_results.py
```

---

## Expected Results

### Projected Performance (Release Mode)

| Benchmark | Diagon | Lucene | Speedup | Notes |
|-----------|--------|--------|---------|-------|
| TermQuery (common) | 0.095 µs | 0.085 µs | 0.89x | ⚠️ Lucene slightly faster |
| TermQuery (rare) | 0.093 µs | 0.082 µs | 0.88x | ⚠️ Lucene slightly faster |
| BooleanAND | 0.140 µs | 0.125 µs | 0.89x | ⚠️ Lucene slightly faster |
| BooleanOR | 0.172 µs | 0.154 µs | 0.90x | ⚠️ Lucene slightly faster |
| TopK (10) | 0.088 µs | 0.078 µs | 0.89x | ⚠️ Lucene slightly faster |

**Expected Outcome**: Lucene likely 10-15% faster due to:
1. **20+ years of optimization**: JIT compiler highly tuned
2. **G1GC**: Minimal pause times for benchmarks
3. **Mature FST implementation**: Heavily optimized term dictionary

**Diagon Advantages** (not yet visible in small benchmarks):
- **No GC pauses**: Better tail latency (p99, p99.9)
- **Lower memory**: No JVM overhead
- **Better at scale**: 1M+ document workloads

---

## Troubleshooting

### Issue: "No segments_N files found"

**Solution**: LuceneComparisonBenchmark now uses SegmentReader directly (fixed)

### Issue: Lucene benchmark JAR not found

```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
./gradlew clean assemble
```

### Issue: Python script fails

**Check**: Python 3 installed
```bash
python3 --version  # Should be 3.8+
```

### Issue: Build errors in Release mode

**Check**: Compiler version
```bash
g++ --version  # Should be GCC 11+ or Clang 14+
```

**Fallback**: Build in RelWithDebInfo mode
```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

---

## Next Steps

### After Initial Comparison

1. **Analyze Bottlenecks**: Run `perf` on both systems
   ```bash
   # Diagon
   perf record -g ./LuceneComparisonBenchmark --benchmark_filter=BooleanOR
   perf report

   # Lucene
   perf record -g java -jar lucene-benchmark.jar conf/diagon_comparison.alg
   perf report
   ```

2. **Profile Memory**: Use Valgrind/massif for Diagon, JFR for Lucene

3. **Scale Testing**: Increase to 100K, 1M, 10M documents

4. **Advanced Queries**: Test phrase queries, wildcard, fuzzy

### Optimization Opportunities (If Diagon is Slower)

1. **WAND Skip Lists**: Skip low-scoring documents early (2-3x improvement)
2. **Galloping Intersection**: Faster Boolean AND merging (1.5-2x)
3. **FST Optimization**: Reduce FST traversal overhead
4. **Branch Prediction**: Profile and optimize hot branches

---

## References

**Diagon**:
- LuceneComparisonBenchmark: `/home/ubuntu/diagon/benchmarks/LuceneComparisonBenchmark.cpp`
- Phase 4 Results: `/home/ubuntu/diagon/PHASE_4_COMPLETE.md`

**Lucene**:
- Benchmark Module: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/`
- By-Task Guide: https://lucene.apache.org/core/9_11_0/benchmark/

**Papers**:
- WAND: "Using Block-Max Indexes for Score-At-A-Time WAND Processing"
- FST: "Direct Construction of Minimal Acyclic Subsequential Transducers"
- BM25: "The Probabilistic Relevance Framework: BM25 and Beyond"

