# Scale Testing: Quick Reference

Everything you need to run comprehensive scale testing comparing Diagon vs Apache Lucene.

---

## Quick Start

```bash
cd /home/ubuntu/diagon
chmod +x RUN_SCALE_TEST.sh
./RUN_SCALE_TEST.sh
```

**Time**: 20 minutes (100K + 1M), or 65 minutes with 10M

---

## What Gets Tested

### Document Scales
- **100K documents**: Small scale (baseline)
- **1M documents**: Medium scale (realistic)
- **10M documents**: Large scale (optional)

### Query Types
1. **TermQuery (common)**: High-frequency term search
2. **TermQuery (rare)**: Low-frequency term search
3. **BooleanAND**: Intersection query
4. **BooleanOR**: Union query
5. **TopK**: Result set size variation (10, 100, 1000)

### Metrics
- Query latency (microseconds)
- Query throughput (QPS)
- Index build time
- Index size on disk
- Memory usage

---

## Files Created

| File | Purpose |
|------|---------|
| **RUN_SCALE_TEST.sh** | Automated test script |
| **SCALE_TEST_GUIDE.md** | Detailed guide with analysis |
| **ScaleComparisonBenchmark.cpp** | Diagon benchmark implementation |
| **benchmarks/CMakeLists.txt** | Updated with new benchmark |

---

## Expected Outcomes

### Hypothesis: Scale Advantage

Diagon should demonstrate **C++ advantages at scale**:

| Scale | Expected Result | Why |
|-------|----------------|-----|
| 100K | Lucene 5-8% faster | JIT optimized, no GC pressure yet |
| 1M | **Parity** | Diagon catches up |
| 10M | **Diagon 10-15% faster** | No GC pauses, better cache utilization |

### Key Metrics to Watch

1. **Speedup Trend**:
   ```
   100K: 0.92x (Lucene faster)
   1M:   1.00x (Parity)
   10M:  1.15x (Diagon faster) ← Goal
   ```

2. **Index Size**:
   - Target: ≤ Lucene (StreamVByte compression)
   - Current: ~8-10% smaller expected

3. **Tail Latency**:
   - p99: Diagon should be more consistent (no GC)
   - p99.9: Bigger difference expected

---

## What the Output Shows

### Index Build Phase

```
=== Building 100K index ===
Adding 100000 documents...
✓ Index built in 8.2 seconds
  Throughput: 12,195 docs/sec
  Index size: 8.5 MB
```

**What to check**:
- Indexing throughput competitive with Lucene
- Index size comparable or smaller

### Query Benchmark Phase

```
BM_Scale_TermQuery/0    0.090 us    11.1M QPS    docs=100000    index_mb=8.5
BM_Scale_TermQuery/1    0.145 us     6.9M QPS    docs=1000000   index_mb=85
```

**What to check**:
- Latency scales sub-linearly (10x docs ≠ 10x latency)
- QPS remains high at all scales

### Comparison Phase

```
SCALE: 100K Documents
                            Diagon (µs)    Lucene (µs)    Speedup    Winner
TermQuery_Common                 90.00          85.00      0.94x    ✓ Lucene
BooleanAND                      135.00         125.00      0.93x    ✓ Lucene

SCALE: 1M Documents
TermQuery_Common                145.00         145.00      1.00x       Tie
BooleanAND                      225.00         230.00      1.02x    ✓ Diagon
```

**What to check**:
- Gap closes or reverses as scale increases
- Boolean queries competitive (important for real use)

---

## Interpreting Results

### Success Criteria

✅ **Good Performance**:
- 100K: ≥ 0.90x speedup (within 10% of Lucene)
- 1M: ≥ 0.95x speedup (within 5%)
- 10M: ≥ 1.00x speedup (match or beat Lucene)

⚠️ **Needs Optimization**:
- Any scale < 0.85x (more than 15% slower)
- No improvement trend across scales
- Index size > 1.2x Lucene

### Common Patterns

**Pattern 1: Constant Gap**
```
100K: 0.90x
1M:   0.90x
10M:  0.90x
```
→ Algorithmic issue, not scale-related

**Pattern 2: Closing Gap** (Target)
```
100K: 0.90x
1M:   0.98x
10M:  1.10x
```
→ C++ advantages emerge at scale ✓

**Pattern 3: Widening Gap**
```
100K: 0.95x
1M:   0.88x
10M:  0.80x
```
→ Scalability problem, investigate

---

## Next Actions Based on Results

### If Diagon Matches/Beats Lucene at Scale ✅

1. **Document Success**:
   - Add results to PHASE_4_COMPLETE.md
   - Update performance claims in README

2. **Focus on Tail Latency**:
   - Add p95/p99/p99.9 measurements
   - Demonstrate GC-free advantage

3. **Scale Further**:
   - Test with MSMarco (8.8M docs)
   - Test with Wikipedia (6M articles)
   - Real-world query logs

### If Diagon is Slower Everywhere ⚠️

1. **Profile Hot Paths**:
   ```bash
   perf record -g ./ScaleComparisonBenchmark --benchmark_filter="TermQuery/1"
   perf report
   ```

2. **Check Obvious Issues**:
   - [ ] Release build? (`-O3 -march=native`)
   - [ ] SIMD enabled? (check AVX2)
   - [ ] CPU governor = performance?

3. **Optimize by Priority**:
   - P0: FST traversal (if >20% CPU time)
   - P1: Postings decoding (if >15% CPU time)
   - P2: BM25 scoring (if >15% CPU time)

### If Boolean Queries Are Slow ⚠️

**Implement Galloping Intersection**:
- Skip many documents at once
- 1.5-2x improvement expected

**Add WAND Skip Lists**:
- Skip low-scoring documents early
- 2-3x improvement for selective queries

---

## Customization

### Test Only Specific Scales

Edit `RUN_SCALE_TEST.sh`:

```bash
# Test only 1M documents
SCALES=(
    # "100000:100K"  # Commented out
    "1000000:1M"
    # "10000000:10M"  # Commented out
)
```

### Add 10M Scale

Uncomment in `ScaleComparisonBenchmark.cpp`:

```cpp
const std::vector<DatasetConfig> DATASETS = {
    {"100K", 100000, 100, "/tmp/diagon_scale_100k"},
    {"1M", 1000000, 100, "/tmp/diagon_scale_1m"},
    {"10M", 10000000, 100, "/tmp/diagon_scale_10m"},  // Uncommented
};
```

And in `RUN_SCALE_TEST.sh`:

```bash
SCALES=(
    "100000:100K"
    "1000000:1M"
    "10000000:10M"  # Uncommented
)
```

**Warning**: 10M requires ~4GB RAM and 30+ minutes

### Test with Real Data (MSMarco)

```cpp
// Add to DATASETS in ScaleComparisonBenchmark.cpp
{"MSMarco", 8841822, 150, "/data/msmarco/index"},

// TODO: Add MSMarco loader
class MSMarcoReader {
    // Read from .jsonl format
    // Parse SPLADE embeddings
};
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Out of memory | Reduce batch size or add swap |
| Build fails | Check compiler version (GCC 11+ or Clang 14+) |
| Lucene hangs | Increase JVM heap: `-Xmx16g` |
| Indexes rebuild every time | Check `/tmp/diagon_scale_*/.built` exists |
| Results vary wildly | Clear caches, disable CPU frequency scaling |

---

## Results Location

```
/tmp/diagon_scale_results/
├── diagon_scale_results.json     # Raw Diagon data
├── diagon_scale_output.txt       # Console output
├── lucene_scale_100K.txt         # Lucene 100K results
├── lucene_scale_1M.txt           # Lucene 1M results
├── lucene_scale_10M.txt          # Lucene 10M results (if run)
└── comparison_report.txt         # Side-by-side comparison
```

---

## Timeline

| Phase | Time | Cumulative |
|-------|------|------------|
| Build Diagon (Release) | 2 min | 2 min |
| Build 100K index | 1 min | 3 min |
| Run 100K queries | 2 min | 5 min |
| Build 1M index | 5 min | 10 min |
| Run 1M queries | 3 min | 13 min |
| Build Lucene benchmark | 1 min | 14 min |
| Generate datasets | 1 min | 15 min |
| Run Lucene 100K | 2 min | 17 min |
| Run Lucene 1M | 3 min | 20 min |
| Parse and compare | <1 min | 20 min |

**Total**: ~20 minutes for 100K + 1M

With 10M: +45 minutes = ~65 minutes total

---

## Success Metrics

After running the test, you should have:

- ✅ Performance data at multiple scales
- ✅ Direct comparison with Lucene
- ✅ Understanding of scale behavior
- ✅ Identified bottlenecks (if any)
- ✅ Optimization roadmap

---

## Follow-Up Tests

### Phase 1: Optimize and Re-test
- Fix identified bottlenecks
- Re-run scale test
- Measure improvement

### Phase 2: Real-World Data
- MSMarco (8.8M docs)
- Wikipedia (6M articles)
- Custom datasets

### Phase 3: Production Validation
- Multi-threaded search
- Concurrent indexing + search
- Long-running stability test

---

## Summary

**Purpose**: Validate that Diagon's C++ implementation delivers competitive performance with Apache Lucene, especially at scale.

**Expected**: Close gap at small scale, match at medium scale, beat at large scale.

**Action**: Run `./RUN_SCALE_TEST.sh` and analyze results.

**Success**: Diagon ≥ 0.90x at 100K, ≥ 1.00x at 10M.

---

For detailed analysis and optimization strategies, see **SCALE_TEST_GUIDE.md**.
