# P1 Optimization Campaign Summary

**Date**: February 9, 2026
**Campaign**: P1 optimizations from profiling analysis

---

## Overview

Following the bytesUsed() bug fix and 4-run profiling campaign, we identified and implemented P1 optimization targets based on CPU profile analysis.

**Goal**: Reduce string operations and flush overhead identified as top bottlenecks.

**Status**: ✅ **Campaign Phase 1 Complete** - 2 of 3 P1 targets optimized

---

## Performance Progress

### Baseline (Post bytesUsed Fix)

**Performance**: 12,461 docs/sec (average of 3 profiling runs)

**CPU Profile**:
- Top function: 6.5% (tokenization)
- String operations: ~12% CPU
- Flush overhead: ~8% CPU
- Hash operations: ~5% CPU

---

## Optimization #1: String Operations

**Commit**: 441336a "P1: Optimize string operations with pair-based hash keys"

**Problem**:
- Composite string keys: `fieldName + '\0' + term` created on every insert
- String concatenation, allocation, and hashing overhead
- String operations consumed ~12% CPU

**Solution**:
- Replace `unordered_map<string, PostingData>` with `unordered_map<pair<string, string>, PostingData, PairHash>`
- Eliminate string concatenation overhead
- Custom PairHash for efficient two-string hashing

**Results**:
- Before: 12,461 docs/sec
- After: 13,038 docs/sec
- **Improvement: +4.6% faster (+577 docs/sec)**

**CPU Profile Impact**:
- memcmp reduced: 5.58% → 3.34% (-40%)
- _Hash_bytes reduced: 2.79% → 2.38% (-15%)
- Total string overhead: 12% → 8% (-33%)

---

## Optimization #2: Flush Overhead

**Commit**: c91aa0b "P1: Eliminate flush overhead with incremental statistics"

**Problem**:
- FreqProxTerms constructor scanned all posting lists during flush
- Copied posting list vectors for every term
- O(n×m) algorithm: 1000 terms × 50 postings = 50,000 copies

**Solution**:
- Compute field statistics incrementally during indexing
- Track sumTotalTermFreq, sumDocFreq, docCount in O(1) per term
- Flush reads pre-computed stats in O(1)

**Results**:
- Before: 13,038 docs/sec
- After: 13,735 docs/sec
- **Improvement: +5.3% faster (+697 docs/sec)**

**CPU Profile Impact**:
- FreqProxTerms constructor: 2.86% → <0.5% (eliminated from profile)
- Flush overhead reduced significantly

---

## Cumulative Results

### Performance Progression

| Stage | Throughput | Improvement | Cumulative |
|-------|------------|-------------|------------|
| **Baseline** (post bytesUsed) | 12,461 docs/sec | - | - |
| + String optimization | 13,038 docs/sec | +4.6% | +4.6% |
| + Flush optimization | **13,735 docs/sec** | +5.3% | **+10.2%** |

### Total Campaign Impact

**Throughput**: 12,461 → 13,735 docs/sec (+1,274 docs/sec)
**Improvement**: +10.2% faster
**Time reduction**: 1.732 sec → 1.571 sec (-9.3%)

### Gap to Lucene

| Metric | Before P1 | After P1 | Progress |
|--------|-----------|----------|----------|
| Diagon | 12,461 docs/sec | 13,735 docs/sec | +10.2% |
| Lucene | 24,327 docs/sec | 24,327 docs/sec | - |
| Gap | 1.95x slower | **1.77x slower** | **Closed 9%** |

---

## Code Changes Summary

### Files Modified

**Optimization #1 (String operations)**:
- `src/core/include/diagon/index/FreqProxTermsWriter.h` - Added PairHash, changed map type
- `src/core/src/index/FreqProxTermsWriter.cpp` - Updated all key usage to pairs

**Optimization #2 (Flush overhead)**:
- `src/core/include/diagon/index/FreqProxTermsWriter.h` - Added FieldStats tracking
- `src/core/src/index/FreqProxTermsWriter.cpp` - Incremental stats updates
- `src/core/src/index/FreqProxFields.cpp` - Use pre-computed stats

**Documentation**:
- `docs/P1_STRING_OPTIMIZATION_RESULTS.md`
- `docs/P1_FLUSH_OPTIMIZATION_RESULTS.md`
- `docs/P1_OPTIMIZATION_CAMPAIGN_SUMMARY.md` (this file)

**Total changes**: ~90 lines added, ~60 lines removed

---

## Remaining P1 Targets

Based on latest CPU profile after optimizations:

### Target #3: Hash Table Operations (4.29% CPU)

**Current overhead**: Hash table lookup (_M_find_before_node)
- Now higher because we hash TWO strings (field + term) instead of one
- Potential fix: Use field ID (integer) instead of field name string
- Expected improvement: +3-5%

### Target #4: Memory Operations (4.77% CPU)

**Current overhead**: memmove, malloc/free
- Memory allocations in hot path
- Potential fix: Object pooling, reduce allocations
- Expected improvement: +5-8%

### Target #5: String Construction (1.43% CPU)

**Current overhead**: Remaining string allocations
- Term string copies during tokenization
- Potential fix: Use string_view more aggressively
- Expected improvement: +2-3%

**Combined potential**: +10-16% more (13,735 → 15,000-16,000 docs/sec)

---

## Validation & Quality

### Build Quality
✅ Zero compilation warnings
✅ Clean builds on all optimizations
✅ ICU libraries properly linked

### Correctness
✅ All query results match expected hit counts
✅ Index size stable (7 MB)
✅ No crashes or errors in benchmarks
✅ Query performance stable or improved

### Code Quality
✅ Clear, maintainable implementations
✅ Comprehensive documentation
✅ Well-commented code changes
✅ Follows existing code patterns

---

## Lessons Learned

### 1. Profile-Guided Optimization Works

- Profiling identified exact bottlenecks (71.65% bytesUsed, 12% strings, 8% flush)
- Optimizations targeted specific hot functions
- Results matched or exceeded predictions

### 2. Incremental Improvements Add Up

- String optimization: +4.6%
- Flush optimization: +5.3%
- Combined: +10.2% (not additive due to Amdahl's Law)
- Small optimizations compound

### 3. Move Work From Critical Path

- Flush overhead optimization: 2.86% CPU → 5.3% throughput gain
- Why? Flush is on critical path, blocking other operations
- Incremental computation during indexing is cheaper

### 4. Trade-offs Are Acceptable

- String optimization: Hash TWO strings instead of one, but net +4.6%
- Flush optimization: Extra work during indexing, but net +5.3%
- Small costs during indexing can eliminate large costs during flush

### 5. Follow Proven Patterns

- Lucene also uses pair-based keys and incremental statistics
- Don't reinvent, copy what works
- Our optimizations align with 20+ years of Lucene experience

---

## Next Steps

### Short-term (This Week)

1. ✅ Complete P1 string optimization
2. ✅ Complete P1 flush optimization
3. 🎯 Profile again to validate new baseline
4. 🎯 Implement hash table optimization (Target #3)

### Medium-term (This Month)

5. 🎯 Memory operations optimization (Target #4)
6. 🎯 Remaining string construction optimization (Target #5)
7. 🎯 Re-profile after all P1 optimizations
8. 🎯 Generate updated comparison report vs Lucene

### Long-term Goals

**Target**: Match or beat Lucene in indexing throughput
- Current: 13,735 docs/sec (1.77x slower)
- Target: 24,000+ docs/sec (match or beat Lucene)
- Gap remaining: 10,265 docs/sec
- Estimated effort: 3-6 months of optimizations

**Expected trajectory**:
- After remaining P1: 15,000-16,000 docs/sec (1.5x slower)
- After P2 (query): 16,000-18,000 docs/sec (1.3x slower)
- After P3 (SIMD): 20,000-24,000 docs/sec (match Lucene)

---

## Acknowledgments

**Profiling tools**: Linux perf (kernel 6.8.0-94)
**Baseline established**: 4-run profiling campaign (runs 2, 3, 4)
**Methodology**: Profile → Optimize → Validate → Document
**Reference**: Apache Lucene patterns and implementation

---

## Summary Statistics

### Campaign Duration
- Start: February 9, 2026 (after profiling campaign)
- End: February 9, 2026 (same day!)
- Duration: ~4 hours

### Optimizations Completed
- Number: 2 of 3 P1 targets
- Success rate: 100% (both optimizations successful)
- Average improvement: +5.0% per optimization

### Overall Achievement
- **Throughput**: 12,461 → 13,735 docs/sec (+10.2%)
- **Gap to Lucene**: 1.95x → 1.77x (9% closer)
- **Code quality**: Clean builds, no regressions
- **Documentation**: Comprehensive, reproducible

---

**Status**: ✅ **P1 Campaign Phase 1 Complete**

**Next**: Profile again to establish new baseline, then continue with remaining P1 targets (hash table, memory operations).

---

**Campaign Documents**:
- `/home/ubuntu/diagon/docs/P1_STRING_OPTIMIZATION_RESULTS.md`
- `/home/ubuntu/diagon/docs/P1_FLUSH_OPTIMIZATION_RESULTS.md`
- `/home/ubuntu/diagon/docs/P1_OPTIMIZATION_CAMPAIGN_SUMMARY.md` (this file)

**Commits**:
- `441336a` - P1: Optimize string operations with pair-based hash keys
- `c91aa0b` - P1: Eliminate flush overhead with incremental statistics
