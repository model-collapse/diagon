# Indexing Performance Profiling Report

**Date**: 2026-02-04
**Task**: P2.1 - Profile indexing performance to identify bottlenecks
**Current Performance**: 1,304 docs/sec
**Target Performance**: >2,000 docs/sec (2x improvement)

---

## Executive Summary

CPU profiling identified **two critical bottlenecks** consuming **87.2% of total CPU time**:

1. **Bottleneck #1 (73.49% CPU)**: O(n²) complexity in `computeNorms()`
2. **Bottleneck #2 (13.71% CPU)**: O(n) complexity in `bytesUsed()` with no caching

Both bottlenecks are in the **flush phase**, not the indexing phase itself.

**Expected Impact**: Optimizing these two bottlenecks should yield **60-80% performance improvement** in end-to-end indexing throughput.

---

## Profiling Methodology

### Tools
- **CPU Profiling**: Linux `perf record` with dwarf call-graph sampling at 999 Hz
- **Workload**: DiagonProfiler indexing 10,000 documents with 10,016 unique terms
- **Build**: Release mode with `-O3 -march=native`, LTO disabled

### Command
```bash
sudo perf record -g -F 999 --call-graph dwarf ./DiagonProfiler
sudo perf report --stdio --no-children
```

### Results
- **Samples**: 7,974 samples
- **Event count**: 28,268,296,660 cycles
- **Duration**: ~10 seconds (indexing + search)

---

## Bottleneck #1: O(n²) computeNorms() - **73.49% CPU**

### Hot Function Breakdown
| Function | CPU % | Description |
|----------|-------|-------------|
| `__memcmp_evex_movbe` | 73.49% | String comparison in getPostingList() |
| `FreqProxTermsWriter::getPostingList()` | 5.78% | Linear scan overhead |
| `__memchr_evex` | 2.79% | Finding null separator in composite keys |
| `__memmove_avx512` | 1.56% | String copying in getPostingList() |
| **Total** | **83.62%** | **Dominated by inefficient lookup** |

### Root Cause Analysis

**File**: `src/core/src/index/DocumentsWriterPerThread.cpp:312-327`

```cpp
std::vector<int64_t> DocumentsWriterPerThread::computeNorms(const std::string& fieldName,
                                                              int numDocs) {
    // Initialize norm values (field length per document)
    std::vector<int64_t> fieldLengths(numDocs, 0);

    // Get all terms from terms writer
    std::vector<std::string> terms = termsWriter_.getTerms();  // ❌ Gets ALL terms from ALL fields

    // Sum frequencies for each document
    for (const auto& term : terms) {
        std::vector<int> postings = termsWriter_.getPostingList(term);  // ❌ O(n) linear scan!

        // Posting list format: [docID, freq, docID, freq, ...]
        for (size_t i = 0; i < postings.size(); i += 2) {
            int docID = postings[i];
            int freq = postings[i + 1];
            fieldLengths[docID] += freq;
        }
    }

    // ...
}
```

**Problem**:
- `getTerms()` returns **all 10,016 terms** from **all fields** (not field-specific)
- `getPostingList(term)` does **O(n) linear scan** through entire map for each term
- **Total complexity**: O(n²) where n = number of unique terms
- **Operations**: ~100 million string comparisons for 10,016 terms

**File**: `src/core/src/index/FreqProxTermsWriter.cpp:115-129`

```cpp
std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& term) const {
    // Legacy method - search all fields for this term
    // This is inefficient but maintains backward compatibility for tests
    for (const auto& [compositeKey, data] : termToPosting_) {  // ❌ Linear scan!
        // Extract term from "field\0term" format
        auto nullPos = compositeKey.find('\0');
        if (nullPos != std::string::npos) {
            std::string termPart = compositeKey.substr(nullPos + 1);
            if (termPart == term) {
                return data.postings;
            }
        }
    }
    return {};  // Term not found
}
```

### Efficient Alternative Already Exists!

**File**: `src/core/src/index/FreqProxTermsWriter.cpp:147-177`

```cpp
// ✅ O(1) hash map lookup
std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& field,
                                                      const std::string& term) const {
    std::string compositeKey = field + '\0' + term;
    auto it = termToPosting_.find(compositeKey);  // ✅ O(1) lookup!
    if (it == termToPosting_.end()) {
        return {};
    }
    return it->second.postings;
}

// ✅ O(n) but only for THIS field
std::vector<std::string> FreqProxTermsWriter::getTermsForField(const std::string& field) const {
    std::vector<std::string> terms;
    std::string prefix = field + '\0';

    for (const auto& [compositeKey, _] : termToPosting_) {
        if (compositeKey.compare(0, prefix.length(), prefix) == 0) {
            terms.push_back(compositeKey.substr(prefix.length()));
        }
    }

    std::sort(terms.begin(), terms.end());
    return terms;
}
```

### Solution

**Change in DocumentsWriterPerThread.cpp:318-322:**

```cpp
// BEFORE (O(n²)):
std::vector<std::string> terms = termsWriter_.getTerms();
for (const auto& term : terms) {
    std::vector<int> postings = termsWriter_.getPostingList(term);
    // ...
}

// AFTER (O(n)):
std::vector<std::string> terms = termsWriter_.getTermsForField(fieldName);  // ✅ Field-specific
for (const auto& term : terms) {
    std::vector<int> postings = termsWriter_.getPostingList(fieldName, term);  // ✅ O(1) lookup
    // ...
}
```

### Expected Impact
- **Complexity**: O(n²) → O(n)
- **CPU Reduction**: 73.49% → ~5% (expected 68% reduction)
- **Speedup**: ~4x faster flush phase
- **End-to-end**: ~2-3x indexing throughput improvement

---

## Bottleneck #2: O(n) bytesUsed() with No Caching - **13.71% CPU**

### Call Stack
```
FreqProxTermsWriter::bytesUsed()
└─ DocumentsWriterPerThread::bytesUsed()
   └─ DocumentsWriterPerThread::needsFlush()
      └─ DocumentsWriter::addDocument()
         └─ IndexWriter::addDocument()
```

**Frequency**: Called on **every document** during indexing to check if flush is needed.

### Root Cause

**File**: `src/core/src/index/FreqProxTermsWriter.cpp:179-193`

```cpp
int64_t FreqProxTermsWriter::bytesUsed() const {
    // ByteBlockPool memory
    int64_t bytes = termBytePool_.bytesUsed();

    // Posting list vectors (approximate)
    for (const auto& [term, data] : termToPosting_) {  // ❌ Iterates 10,016 entries every call!
        bytes += term.capacity();
        bytes += data.postings.capacity() * sizeof(int);
    }

    // Map overhead (approximate)
    bytes += termToPosting_.size() * 64;

    return bytes;
}
```

**Problem**:
- Iterates through **all 10,016 terms** on every call
- Called **10,000 times** during indexing (once per document)
- **Total iterations**: 10,016 × 10,000 = **100,160,000 map traversals**
- No caching, recalculates from scratch every time

### Solution

Add cached value updated incrementally:

```cpp
class FreqProxTermsWriter {
private:
    mutable int64_t cachedBytesUsed_ = 0;  // Cached memory usage
    mutable bool bytesUsedDirty_ = true;   // Cache validity flag

public:
    int64_t bytesUsed() const {
        if (!bytesUsedDirty_) {
            return cachedBytesUsed_;  // ✅ O(1) cached return
        }

        // Recalculate only when dirty
        int64_t bytes = termBytePool_.bytesUsed();
        for (const auto& [term, data] : termToPosting_) {
            bytes += term.capacity();
            bytes += data.postings.capacity() * sizeof(int);
        }
        bytes += termToPosting_.size() * 64;

        cachedBytesUsed_ = bytes;
        bytesUsedDirty_ = false;
        return bytes;
    }

    void invalidateBytesUsedCache() {
        bytesUsedDirty_ = true;  // ✅ Called when adding terms
    }
};
```

**Update points**: Call `invalidateBytesUsedCache()` in:
- `addTerm()` - When new term added
- `appendToPostingList()` - When posting list grows

### Expected Impact
- **Complexity**: O(n) per call → O(1) cached
- **CPU Reduction**: 13.71% → ~0.1% (expected 13.6% reduction)
- **Speedup**: ~7x faster memory tracking

---

## Additional Observations

### Low-Impact Areas (No Optimization Needed)
| Function | CPU % | Note |
|----------|-------|------|
| VByte decode | 0.09% | Already efficient |
| Hash computation | 0.06% | Not a bottleneck |
| String allocation | 0.08% | Minimal impact |

### Search Performance (Not a Bottleneck)
- Search queries consume <5% of total time
- Already optimized (0.142ms P99)
- No action needed

---

## Optimization Priority

| Priority | Bottleneck | CPU Impact | Complexity | Estimated Effort |
|----------|-----------|------------|------------|------------------|
| **P0** | computeNorms() O(n²) | 73.49% | Low (1 line change) | 15 minutes |
| **P1** | bytesUsed() caching | 13.71% | Low (add cache) | 30 minutes |

**Total expected improvement**: **60-80% indexing throughput increase**

---

## Validation Plan

### Micro-Benchmark Before/After
```cpp
// Benchmark: computeNorms() with 10K terms
BENCHMARK(BM_ComputeNorms_Before)->Range(1000, 10000);
BENCHMARK(BM_ComputeNorms_After)->Range(1000, 10000);

// Benchmark: bytesUsed() call overhead
BENCHMARK(BM_BytesUsed_Before)->Iterations(10000);
BENCHMARK(BM_BytesUsed_After)->Iterations(10000);
```

### End-to-End Validation
```bash
# Run full benchmark before optimization
./DiagonProfiler > before_results.txt

# Apply optimizations

# Run full benchmark after optimization
./DiagonProfiler > after_results.txt

# Compare
python3 scripts/compare_benchmarks.py before_results.txt after_results.txt
```

**Target Metrics**:
- Indexing throughput: 1,304 docs/sec → **>2,000 docs/sec** (2x target)
- Flush time: ~5-6s → **~1-2s** (3-5x faster)
- P99 search: 0.142ms → **<0.15ms** (maintain current performance)

---

## Memory Profiling (Next Step)

Valgrind Massif profiling deferred until after CPU optimizations to avoid profiling inefficient code.

**Planned command**:
```bash
valgrind --tool=massif --massif-out-file=massif_diagon.out ./DiagonProfiler
ms_print massif_diagon.out > diagon_memory_profile.txt
```

---

## Conclusion

**Key Findings**:
1. ✅ Indexing hot spots identified with 73.49% + 13.71% = **87.2% CPU** accounted for
2. ✅ Both bottlenecks have **simple, low-risk fixes** (no architectural changes needed)
3. ✅ Efficient alternatives already exist in codebase (use field-specific methods)
4. ✅ Expected 2-3x improvement achievable with **<1 hour of work**

**Next Steps**:
1. Implement computeNorms() fix (P0)
2. Implement bytesUsed() caching (P1)
3. Validate with micro-benchmarks
4. Re-run full benchmark suite
5. Verify 2x indexing throughput achieved

---

**Profiling Data**: `/tmp/diagon_index_perf.data`
**Report Generated**: 2026-02-04
**Status**: Ready for optimization (Task #26)
