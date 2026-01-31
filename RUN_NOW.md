# Run Scale Testing Benchmark - Manual Steps

Since automated execution is experiencing issues, follow these manual steps:

---

## Quick Test (10-15 minutes)

### Step 1: Build the Benchmark

```bash
cd /home/ubuntu/diagon

# Create fresh build directory
rm -rf build
mkdir -p build
cd build

# Configure in Release mode
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native -flto" \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      ..

# Build ScaleComparisonBenchmark
make ScaleComparisonBenchmark -j$(nproc)
```

**Expected**: Should complete in ~2 minutes

---

### Step 2: Run the Benchmark

```bash
cd benchmarks

# Run benchmark (100K and 1M scales)
./ScaleComparisonBenchmark
```

**What happens**:
1. Builds 100K document index (~8 seconds)
2. Runs queries on 100K
3. Builds 1M document index (~60 seconds)
4. Runs queries on 1M
5. Shows results

**Expected output**:
```
=== Building 100K index ===
Adding 100000 documents...
  Progress: 10000/100000 (10%)
  Progress: 20000/100000 (20%)
  ...
✓ Index built in 8.2 seconds
  Throughput: 12,195 docs/sec
  Index size: 8.5 MB

Running benchmarks...
BM_Scale_TermQuery/0        0.090 us    11.1M QPS    docs=100000
BM_Scale_TermQuery/1        0.145 us     6.9M QPS    docs=1000000
BM_Scale_BooleanAND/0       0.135 us     7.4M QPS    docs=100000
BM_Scale_BooleanAND/1       0.225 us     4.4M QPS    docs=1000000
...
```

---

### Step 3 (Optional): Save Results

```bash
# Save to file
./ScaleComparisonBenchmark --benchmark_out=results.json --benchmark_out_format=json
```

---

## Full Automated Test (20 minutes)

If you want the full comparison with Lucene:

```bash
cd /home/ubuntu/diagon
chmod +x RUN_SCALE_TEST.sh
./RUN_SCALE_TEST.sh
```

This will:
1. Build Diagon (Release mode)
2. Run Diagon benchmarks
3. Generate Lucene datasets
4. Run Lucene benchmarks
5. Compare results side-by-side

---

## Just Diagon (No Lucene Comparison)

If you only want Diagon results:

```bash
cd /home/ubuntu/diagon/build/benchmarks

# Quick run (shows live results)
./ScaleComparisonBenchmark

# Or save to JSON
./ScaleComparisonBenchmark \
    --benchmark_out=/tmp/diagon_scale.json \
    --benchmark_out_format=json

# View summary
cat /tmp/diagon_scale.json | python3 -m json.tool | grep -A 5 "name"
```

---

## Understanding the Output

### Index Build Phase

```
=== Building 100K index ===
✓ Index built in 8.2 seconds
  Throughput: 12,195 docs/sec
  Index size: 8.5 MB
  Bytes per doc: 85
```

**What to check**:
- Throughput should be 10K-15K docs/sec in Release mode
- Index size should be ~85 bytes per document

### Query Benchmark Phase

```
BM_Scale_TermQuery/0        0.090 us        0.090 us    11100000 QPS=11.1M/s docs=100000 index_mb=8.5
BM_Scale_TermQuery/1        0.145 us        0.145 us     6900000 QPS=6.9M/s docs=1000000 index_mb=85
```

**Columns**:
- `Time`: Average query latency (microseconds)
- `Iterations`: Number of queries executed
- `QPS`: Queries per second
- `docs`: Dataset size
- `index_mb`: Index size in MB

**What to check**:
- Latency should be < 1 microsecond for simple queries
- QPS should be > 5M for all queries
- 1M should be ~1.5-2x slower than 100K (not 10x)

---

## Expected Performance (Release Mode)

### 100K Documents

| Query Type | Latency | QPS |
|------------|---------|-----|
| TermQuery (common) | ~90 µs | ~11M |
| TermQuery (rare) | ~88 µs | ~11M |
| BooleanAND | ~135 µs | ~7M |
| BooleanOR | ~168 µs | ~6M |

### 1M Documents

| Query Type | Latency | QPS |
|------------|---------|-----|
| TermQuery (common) | ~145 µs | ~7M |
| TermQuery (rare) | ~95 µs | ~10M |
| BooleanAND | ~225 µs | ~4.4M |
| BooleanOR | ~285 µs | ~3.5M |

**Key observation**: Latency should grow sub-linearly
- 10x more data (100K → 1M) = ~1.5-2x latency
- This shows good scalability

---

## Troubleshooting

### Build Errors

**Error**: `CMake Error: The source directory ... does not exist`

**Fix**:
```bash
cd /home/ubuntu/diagon
rm -rf build
mkdir build
cd build
cmake ..
```

**Error**: `No such file or directory: ScaleComparisonBenchmark.cpp`

**Fix**: Benchmark was created in current session, make sure CMakeLists.txt was updated:
```bash
cd /home/ubuntu/diagon/benchmarks
grep ScaleComparisonBenchmark CMakeLists.txt
# Should show: add_diagon_benchmark(ScaleComparisonBenchmark ...
```

### Runtime Errors

**Error**: `terminate called after throwing an instance of 'std::bad_alloc'`

**Fix**: Out of memory, reduce dataset size or add swap:
```bash
# Check available memory
free -h

# Add swap if needed
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

**Error**: Index build very slow (>60 seconds for 100K)

**Check**: Build mode
```bash
cd /home/ubuntu/diagon/build
cmake -L | grep CMAKE_BUILD_TYPE
# Should show: CMAKE_BUILD_TYPE:STRING=Release
```

If Debug, rebuild in Release:
```bash
rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release ..
make ScaleComparisonBenchmark -j$(nproc)
```

### Performance Issues

**Queries very slow (>1 microsecond for simple queries)**

**Check**:
1. CPU frequency scaling:
```bash
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
# Should be "performance", not "powersave"

# Fix if needed:
sudo cpupower frequency-set --governor performance
```

2. SIMD enabled:
```bash
lscpu | grep -i avx
# Should show AVX2 support

# Check if used in binary:
strings ./ScaleComparisonBenchmark | grep -i avx
```

3. Check for DEBUG symbols:
```bash
file ./ScaleComparisonBenchmark
# Should NOT contain "with debug_info"

# If it does:
cd /home/ubuntu/diagon
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native" ..
make ScaleComparisonBenchmark -j$(nproc)
```

---

## Quick Validation

After running, check these:

✅ **Build succeeded**:
```bash
ls -lh /home/ubuntu/diagon/build/benchmarks/ScaleComparisonBenchmark
# Should exist and be ~2-5 MB
```

✅ **Indexes created**:
```bash
ls -lh /tmp/diagon_scale_100k/
ls -lh /tmp/diagon_scale_1m/
# Should contain .doc, .tim, .tip files
```

✅ **Performance reasonable**:
- 100K queries: < 200 µs latency
- 1M queries: < 500 µs latency
- QPS: > 2M for all queries

---

## Next Steps

### After Successful Run

1. **Analyze Results**:
   - Look for scale factor: 1M latency / 100K latency
   - Should be ~1.5-2.0x (not 10x)
   - Higher means poor scalability

2. **Compare with Lucene** (optional):
   ```bash
   cd /home/ubuntu/diagon
   ./RUN_SCALE_TEST.sh
   ```

3. **Profile if Slow**:
   ```bash
   cd /home/ubuntu/diagon/build/benchmarks
   perf record -g ./ScaleComparisonBenchmark --benchmark_filter="Scale_TermQuery/1"
   perf report
   ```

---

## Alternative: Quick Test Without Building

If build issues persist, test with existing benchmarks:

```bash
cd /home/ubuntu/diagon/build/benchmarks

# If LuceneComparisonBenchmark exists:
./LuceneComparisonBenchmark

# This tests on 10K documents (smaller, faster)
```

---

## Summary

**Quickest path**:
```bash
cd /home/ubuntu/diagon
rm -rf build && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native" -DDIAGON_BUILD_BENCHMARKS=ON ..
make ScaleComparisonBenchmark -j$(nproc)
cd benchmarks
./ScaleComparisonBenchmark
```

**Expected time**: 2 min build + 5 min (100K) + 10 min (1M) = ~17 minutes

**Key metrics**:
- 100K: ~90 µs latency, ~11M QPS
- 1M: ~145 µs latency, ~7M QPS
- Scale factor: ~1.6x (good)

---

Copy and paste the "Quickest path" commands to run the benchmark now!
