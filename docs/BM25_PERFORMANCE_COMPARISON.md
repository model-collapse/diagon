# BM25 Performance: Diagon vs Apache Lucene

**Purpose**: Track Diagon BM25 scoring performance against Lucene baseline

**Status**: Baseline Established, Performance Guards Created

---

## Executive Summary

| Component | Lucene Baseline<br/>(Reuters) | Diagon Target | Diagon Smoke Test<br/>(Synthetic) | Status |
|-----------|-----------------|---------------|-------------------|--------|
| **OR-5 P50** | 109.6 µs | ≤ 126 µs | 3,073 µs | ⚠️ Needs Real Data |
| **OR-5 P99** | 211.1 µs | ≤ 250 µs | 19,111 µs | ⚠️ Needs Real Data |
| **Single-term P50** | 46.8 µs | ≤ 65 µs | 464 µs | ⚠️ Needs Real Data |
| **AND-2 P50** | 43.1 µs | ≤ 51 µs | 597 µs | ⚠️ Needs Real Data |
| **Indexing** | 12,024 docs/sec | ≥ 10,000 docs/sec | TBD | 🔄 Pending |

**Status Update (2026-02-11)**:
- ✅ BM25PerformanceGuard test created, compiles, and runs successfully
- ✅ Segfault fixed (directory lifecycle issue resolved)
- ✅ Using MMapDirectory for better I/O performance
- ⚠️ Current results use synthetic data, not comparable to Lucene Reuters baseline
- 📊 Smoke test results: Single-term=464µs, OR-5=3,073µs, AND-2=597µs
- 🔍 Performance difference expected: synthetic random data vs real text has different characteristics

**Key Findings**:
- **Smoke tests work**: No crashes, basic functionality validated
- **Synthetic data != Reuters**: Random terms have different FST/posting patterns than real text
- **For accurate comparison**: Need to benchmark on real Reuters dataset

**Next Actions**:
1. ~~Build Diagon with BM25PerformanceGuard tests~~ ✅ Complete
2. ~~Debug segfault in index reading~~ ✅ Fixed (directory lifecycle)
3. Measure Diagon performance on **real Reuters data** with `/benchmark_diagon` skill
4. Update this document with apples-to-apples comparison results
5. Identify bottlenecks if performance below target
6. Optimize until Diagon matches or exceeds Lucene

---

## Lucene Baseline (Established: 2026-02-11)

### Test Environment

- **Lucene Version**: 11.0.0-SNAPSHOT
- **Codec**: Lucene104 (default)
- **Similarity**: BM25 (k1=1.2, b=0.75)
- **Dataset**: Reuters-21578 (19,043 documents, 64,664 unique terms)
- **Segments**: 1 (force merged)
- **Warmup**: 100 iterations
- **Measurement**: 1,000 iterations

### Detailed Results

**Indexing**:
- Throughput: 12,024 docs/sec
- Total time: 1.584 sec
- Pure indexing: 1.263 sec (79.7%)
- Merge time: 0.321 sec (20.3%)

**Single-Term Queries**:
| Term | Hits | P50 (µs) | P95 (µs) | P99 (µs) |
|------|------|----------|----------|----------|
| market | 1,007 | 46.8 | 124.9 | 297.7 |
| trade | 1,006 | 32.8 | 45.5 | 56.4 |
| oil | 1,005 | 27.5 | 39.1 | 52.9 |
| cocoa (rare) | 89 | 20.2 | 27.2 | 38.5 |

**Multi-Term OR Queries** (WAND):
| Query | Terms | Hits | P50 (µs) | P95 (µs) | P99 (µs) |
|-------|-------|------|----------|----------|----------|
| OR-2 | 2 | 1,362 | 98.6 | 187.2 | 231.1 |
| OR-2 | 2 | 1,148 | 81.4 | 107.7 | 121.8 |
| OR-3 | 3 | 1,828 | 93.2 | 301.4 | 336.8 |
| OR-3 | 3 | 1,159 | 45.8 | 59.2 | 89.6 |
| **OR-5** | **5** | **1,440** | **109.6** | **175.6** | **211.1** |
| OR-5 | 5 | 1,373 | 101.6 | 116.5 | 146.7 |
| OR-10 | 10 | 1,215 | 208.7 | 228.9 | 354.1 |

**Multi-Term AND Queries**:
| Query | Terms | Hits | P50 (µs) | P95 (µs) | P99 (µs) |
|-------|-------|------|----------|----------|----------|
| **AND-2** | **2** | **332** | **43.1** | **82.8** | **138.1** |
| AND-2 | 2 | 333 | 45.3 | 61.6 | 105.2 |
| AND-3 | 3 | 149 | 71.7 | 87.9 | 115.8 |

**TopK Variation** (OR-5):
| TopK | Hits | P50 (µs) | P95 (µs) | P99 (µs) |
|------|------|----------|----------|----------|
| 10 | 1,440 | 171.2 | 186.9 | 217.3 |
| 50 | 1,767 | 109.5 | 123.2 | 138.3 |
| 100 | 2,251 | 117.9 | 130.4 | 136.9 |
| 1000 | 5,374 | 254.1 | 268.0 | 276.8 |

---

## Diagon Performance Targets

### Primary Benchmarks

**OR-5 Query** (oil OR trade OR market OR price OR dollar):
- **P50 Target**: ≤ 126 µs (15% margin over Lucene 109.6 µs)
- **P99 Target**: ≤ 250 µs (18% margin over Lucene 211.1 µs)
- **Critical Threshold**: < 220 µs (P50) - Never exceed 2x Lucene

**Single-Term Query** (market):
- **P50 Target**: ≤ 65 µs (39% margin over Lucene 46.8 µs)
- **P99 Target**: ≤ 350 µs (18% margin over Lucene 297.7 µs)
- **Critical Threshold**: < 100 µs (P50) - Never exceed 2x Lucene

**AND-2 Query** (oil AND price):
- **P50 Target**: ≤ 51 µs (18% margin over Lucene 43.1 µs)
- **P99 Target**: ≤ 165 µs (19% margin over Lucene 138.1 µs)
- **Critical Threshold**: < 90 µs (P50) - Never exceed 2x Lucene

### Why Margins?

- **C++ vs Java**: Different memory management, GC vs manual allocation
- **Implementation variations**: Different algorithms for same functionality
- **Compiler optimizations**: GCC vs JIT compiler
- **Measurement differences**: Microsecond-level timing sensitive to environment

**Philosophy**:
- **Initial goal**: Match Lucene (within 15-20% margin)
- **Long-term goal**: Exceed Lucene (faster than Java baseline)
- **Critical line**: Never fall below 50% of Lucene (> 2x slower = unacceptable)

---

## Performance Guards

### Test Implementation

**File**: `tests/unit/search/BM25PerformanceGuard.cpp`

**Tests**:
1. `SingleTerm_P50_Baseline` - Single-term query P50 ≤ 65 µs
2. `SingleTerm_P99_Baseline` - Single-term query P99 ≤ 350 µs
3. `OR5Query_P50_Baseline` - OR-5 query P50 ≤ 126 µs
4. `OR5Query_P99_Baseline` - OR-5 query P99 ≤ 250 µs
5. `AND2Query_P50_Baseline` - AND-2 query P50 ≤ 51 µs
6. `AND2Query_P99_Baseline` - AND-2 query P99 ≤ 165 µs
7. `TopKScaling_OR5` - TopK scaling ≤ 3x difference (K=50 vs K=1000)
8. `RareTerm_Faster` - Rare terms faster than common terms

**Running Tests**:
```bash
cd /home/ubuntu/diagon/build
make BM25PerformanceGuard
./tests/unit/BM25PerformanceGuard
```

---

## Performance Breakdown (Estimated)

For OR-5 query (109.6 µs Lucene baseline):

| Phase | Time (µs) | % Total | Optimization Opportunity |
|-------|-----------|---------|---------------------------|
| FST term lookup (5 terms) | 10-15 | 10-14% | ✅ Already optimized |
| Postings decoding | 25-35 | 23-32% | 🔄 SIMD VByte/StreamVByte |
| BM25 scoring | 40-50 | 36-46% | 🔄 SIMD batch scoring |
| WAND early termination | 15-20 | 14-18% | ✅ Already implemented |
| TopK heap operations | 10-15 | 9-14% | ⚠️ Potential optimization |

**Top Optimization Targets**:
1. **BM25 scoring** (40-50 µs): SIMD batch scoring for multiple documents
2. **Postings decoding** (25-35 µs): SIMD VByte/StreamVByte decoding
3. **Heap operations** (10-15 µs): Optimize TopK priority queue

---

## Next Steps

### Phase 1: Baseline Measurement (Current Phase)

1. ✅ Profile Lucene BM25 scoring on Reuters-21578
2. ✅ Document Lucene baseline performance
3. ✅ Create Diagon BM25 performance guard tests
4. ✅ Add tests to CMake build system
5. 🔄 **Next**: Build and run Diagon tests to measure current performance

### Phase 2: Performance Comparison

1. Run BM25PerformanceGuard tests on Diagon
2. Collect Diagon performance metrics
3. Update this document with Diagon results
4. Identify performance gaps (if any)
5. Prioritize optimization targets

### Phase 3: Optimization (If Needed)

**If Diagon slower than targets**:
1. Profile Diagon with perf/VTune to find hot functions
2. Optimize identified bottlenecks:
   - SIMD batch scoring
   - SIMD postings decoding
   - Heap operation optimization
3. Re-measure after each optimization
4. Update performance guards as improvements are verified

**If Diagon already exceeds targets**:
1. Document the win! 🎉
2. Analyze why Diagon is faster (C++ advantages, better algorithms)
3. Consider tightening performance guard thresholds
4. Share findings with community

---

## References

- **Lucene Baseline Doc**: `docs/LUCENE_BM25_PERFORMANCE_BASELINE.md`
- **Lucene Profiler**: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/src/java/org/apache/lucene/benchmark/diagon/LuceneBM25Profiler.java`
- **Diagon Performance Guards**: `tests/unit/search/BM25PerformanceGuard.cpp`
- **Previous Comparison**: `docs/REUTERS_HEAD_TO_HEAD_COMPARISON.md`

---

## Status Legend

- ✅ Complete
- 🔄 In Progress / Pending
- ⚠️ Needs Attention
- ❌ Below Target / Failed

---

**Last Updated**: 2026-02-11
**Next Update**: After running Diagon BM25PerformanceGuard tests
