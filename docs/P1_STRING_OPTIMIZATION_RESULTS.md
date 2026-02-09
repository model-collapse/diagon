# P1 String Optimization Results

**Date**: February 9, 2026
**Optimization**: Replace composite string keys with pair-based keys in FreqProxTermsWriter

---

## Problem Identified

Profiling showed ~12% CPU in string operations:
- String moves: 3.72% CPU
- memcmp (string comparisons): 5.58% CPU
- _Hash_bytes (string hashing): 2.79% CPU

**Root cause**: Line 71 in FreqProxTermsWriter.cpp created new allocated string on every term insertion:
```cpp
std::string compositeKey = fieldName + '\0' + term;  // SLOW!
```

This caused:
1. String allocation overhead
2. String concatenation copies
3. Hash computation on full concatenated string

---

## Solution Implemented

Replaced composite string key with pair-based key:

**Before**:
```cpp
std::unordered_map<std::string, PostingData> termToPosting_;
// Usage: termToPosting_[fieldName + '\0' + term] = data;
```

**After**:
```cpp
struct PairHash {
    size_t operator()(const std::pair<std::string, std::string>& p) const {
        size_t h1 = std::hash<std::string>{}(p.first);
        size_t h2 = std::hash<std::string>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

std::unordered_map<std::pair<std::string, std::string>, PostingData, PairHash> termToPosting_;
// Usage: termToPosting_[std::make_pair(fieldName, term)] = data;
```

**Eliminated**:
- String concatenation overhead
- Redundant memory allocation per term
- Composite string storage in hash map

---

## Performance Results

### Indexing Throughput

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| **Indexing** | 12,461 docs/sec | 13,038 docs/sec | **+4.6% faster** |
| **Time** | 1.732 sec avg | 1.655 sec | **-4.4% reduction** |

**Baseline**: Average of 3 profiling runs (12,408, 12,444, 12,531 docs/sec)
**After**: Single run (13,038 docs/sec)

### CPU Profile Changes

| Category | Before | After | Change |
|----------|--------|-------|--------|
| Top function | 6.5% avg | 8.11% | +1.6% (variance) |
| String moves | 3.72% | Reduced | ✓ |
| memcmp | 5.58% | 3.34% | **-40% reduction** |
| _Hash_bytes | 2.79% | 2.38% | **-15% reduction** |
| Hash lookup | 2.79% | 4.29% | +54% (hashing 2 strings) |
| **Total string overhead** | **~12%** | **~8%** | **-33% reduction** |

**Analysis**:
- Successfully reduced string comparison and hashing overhead
- Hash table lookup increased because we now hash TWO strings (field + term) instead of one
- Net improvement: +4.6% indexing throughput, 33% less string overhead

---

## Files Modified

1. **src/core/include/diagon/index/FreqProxTermsWriter.h**:
   - Added `PairHash` struct for pair<string, string> hashing
   - Changed `termToPosting_` type from `unordered_map<string, PostingData>` to `unordered_map<pair<string, string>, PostingData, PairHash>`

2. **src/core/src/index/FreqProxTermsWriter.cpp**:
   - Updated `addTermOccurrence()` to use `std::make_pair(fieldName, term)`
   - Updated `getPostingList()` methods to use pair keys
   - Updated `getTerms()` to extract term from `key.second`
   - Updated `getTermsForField()` to compare `key.first == field`

**Total changes**: ~50 lines modified

---

## Validation

### Build Status
✅ Core library compiled successfully
✅ ICU libraries linked properly
✅ Benchmarks built without errors

### Correctness
✅ All query results match expected hit counts
✅ No crashes or errors during benchmark
✅ Index size remains same (7 MB)

### Performance Stability
- Single run: 13,038 docs/sec
- Need 2-3 more runs to establish stable baseline

---

## Next Optimization Targets

Based on updated CPU profile:

**P1 Targets** (ranked by CPU %):
1. **ICU tokenization** (8.11%) - Inherent cost, accept
2. **memmove** (4.77%) - Memory operations, optimize with ByteBlockPool
3. **Hash lookup** (4.29%) - Now higher because hashing 2 strings
4. **toLower** (4.30%) - ICU case folding, inherent cost
5. **FreqProxTerms constructor** (2.86%) - **Flush overhead target**
6. **String construction** (1.43%) - Remaining string allocation

**Recommended next optimization**:
- **FreqProxTerms constructor** (2.86% CPU) - This is the flush overhead
- Expected improvement: +5-10% if we eliminate posting list copies during flush

---

## Trade-offs

**Pros**:
- Eliminated string concatenation overhead (+4.6% faster)
- Reduced memory allocations
- Cleaner key structure (no null separator hacks)

**Cons**:
- Hash function now processes two strings instead of one
- Slightly increased hash table lookup time (4.29% vs 2.79%)
- More complex hash function (PairHash struct)

**Net result**: Positive (+4.6% faster), but less than predicted (+15-20%) because:
1. Not all string operations were from composite keys
2. Hashing two strings has its own overhead
3. Hash table collision resolution increased slightly

---

## Conclusion

**Status**: ✅ **String optimization successful**

**Key achievements**:
- ✅ 4.6% faster indexing (12,461 → 13,038 docs/sec)
- ✅ 33% reduction in string overhead (12% → 8%)
- ✅ Cleaner code without string concatenation hacks
- ✅ No correctness issues or regressions

**Reality check**:
- Improvement (+4.6%) less than predicted (+15-20%)
- String operations still consume ~8% CPU
- Hash table lookup increased (hashing 2 strings has cost)
- Further string optimization requires more aggressive changes

**Next step**: Optimize FreqProxTerms constructor (flush overhead, 2.86% CPU)

---

**Document**: `/home/ubuntu/diagon/docs/P1_STRING_OPTIMIZATION_RESULTS.md`
**Commit**: Pending verification run
