# StreamVByte Reader Optimization

## Summary

Optimized Lucene104PostingsEnum buffer size from 4 to 32 docs to reduce refill overhead, achieving **1.81× speedup** in posting list decoding.

**Date**: 2026-01-26
**Status**: ✅ Complete - All tests passing, 1.81× faster

## Problem

Initial StreamVByte reader implementation had high overhead:
- **Before optimization**: 36 M items/s
- **VInt baseline**: 104 M items/s
- **Gap**: 2.89× slower than VInt ❌

**Root cause**: Buffer size of only 4 docs caused 250 refills per 1000 docs, with each refill having overhead:
- Function call
- Control byte parsing (twice per group - docs + freqs)
- Multiple small I/O operations
- Buffer management

## Optimizations Implemented

### 1. Increased Buffer Size (4 → 32 docs)
- **Before**: Buffer 4 docs (1 StreamVByte group)
- **After**: Buffer up to 32 docs (8 StreamVByte groups)
- **Effect**: 8× fewer refills (31 refills per 1000 docs instead of 250)

### 2. Batch Decode Multiple Groups
Changed `refillBuffer()` to decode multiple StreamVByte groups in one call:

```cpp
// BEFORE: Decode single group of 4
if (remaining >= BUFFER_SIZE) {
    // Decode 4 docs
    bufferLimit_ = BUFFER_SIZE;
}

// AFTER: Decode up to 8 groups (32 docs)
while (remaining >= STREAMVBYTE_GROUP_SIZE &&
       bufferIdx + STREAMVBYTE_GROUP_SIZE <= BUFFER_SIZE) {
    // Decode 4 docs into buffer[bufferIdx]
    bufferIdx += STREAMVBYTE_GROUP_SIZE;
}
bufferLimit_ = bufferIdx;
```

### 3. Fixed Buffer Overflow Bug
Added bounds checking to prevent VInt fallback from writing past buffer end:

```cpp
// Check available space before VInt fallback
int spaceLeft = BUFFER_SIZE - bufferIdx;
int docsToRead = std::min(remaining, spaceLeft);
```

### 4. Simplified Frequency Access
Changed branching ternary to simpler form in `nextDoc()`:

```cpp
// Slightly cleaner (though minimal performance impact)
currentFreq_ = writeFreqs_ ? freqBuffer_[bufferPos_] : 1;
```

## Performance Results

### Decode Performance (Release Build)

| Test Size | Before (4-doc) | After (32-doc) | Improvement | VInt Baseline |
|-----------|----------------|----------------|-------------|---------------|
| 100 docs  | -              | 62.6 M/s       | -           | 102.6 M/s     |
| 1000 docs | 34.5 M/s       | 65.4 M/s       | **1.89×**   | 104.3 M/s     |
| 10000 docs| 35.9 M/s       | 65.5 M/s       | **1.82×**   | 104.3 M/s     |
| 100000 docs| 36.1 M/s      | 65.4 M/s       | **1.81×**   | 104.1 M/s     |

**Key findings**:
- ✅ **1.81-1.89× speedup** from buffer size optimization
- ⚠️ Still **1.59× slower** than VInt baseline (65 M/s vs 104 M/s)
- Raw StreamVByte decode: **348 M items/s** (1.72× faster than raw VInt at 202 M/s)

### Overhead Analysis

| Component | Time per doc | Percentage |
|-----------|--------------|------------|
| **Before optimization** | 27.7 ns | 100% |
| Raw decode | 2.87 ns | 10.4% |
| Reader overhead | 24.8 ns | 89.6% ❌ |
| **After optimization** | 15.3 ns | 100% |
| Raw decode | 2.87 ns | 18.8% |
| Reader overhead | 12.4 ns | 81.2% ⚠️ |
| **VInt baseline** | 9.6 ns | 100% |

**Interpretation**:
- Reduced overhead by 50% (24.8 ns → 12.4 ns) ✅
- But overhead still dominates decode time (81% vs 19%) ⚠️
- Gap to VInt: 5.7 ns per doc

## Files Modified

### Headers
- `src/core/include/diagon/codecs/lucene104/Lucene104PostingsReader.h`
  - Changed `BUFFER_SIZE` from 4 to 32
  - Added `STREAMVBYTE_GROUP_SIZE` constant
  - Updated documentation

### Implementation
- `src/core/src/codecs/lucene104/Lucene104PostingsReader.cpp`
  - Rewrote `refillBuffer()` to decode multiple groups
  - Added buffer overflow protection
  - Simplified frequency access in `nextDoc()`

## Test Results

All 34 posting list tests pass:
- ✅ PostingsWriterTest: 13/13
- ✅ PostingsReaderTest: 8/8
- ✅ StreamVBytePostingsDebugTest: 1/1
- ✅ StreamVBytePostingsRoundTripTest: 4/4
- ✅ PostingsWriterReaderRoundTripTest: 8/8

## Remaining Performance Gap

Despite the 1.81× improvement, we're still 1.59× slower than VInt. The gap comes from:

### 1. Per-Doc Overhead in nextDoc() (~3-4 ns)
- Buffer position check: `if (bufferPos_ >= bufferLimit_)`
- Delta encoding branch: `if (currentDoc_ == -1)`
- Frequency conditional: `writeFreqs_ ?`
- Array indexing: `docDeltaBuffer_[bufferPos_]`, `freqBuffer_[bufferPos_]`

### 2. Refill Overhead (~8-9 ns per refill, amortized)
- While loop condition checks
- Control byte parsing (2 loops of 4 iterations each)
- I/O operations (readByte, readBytes)
- StreamVByte::decode4 function calls

### 3. VInt Advantages
- Inline decoding in IndexInput (no function call)
- Single value at a time (no buffering overhead)
- Branch predictor friendly (sequential access pattern)
- Simpler state machine

## Further Optimization Opportunities

### P4: Inline Decode (Not Implemented)
Inline StreamVByte::decode4() directly into refillBuffer() to eliminate function call overhead.

**Estimated gain**: 1-2 ns per doc (~10-15%)

### P5: Branchless nextDoc() (Not Implemented)
Eliminate branches using:
- Sentinel value for first doc (currentDoc_ = 0 instead of -1)
- Lookup tables for frequency

**Estimated gain**: 1 ns per doc (~7%)

### P6: Prefetch Next Buffer (Not Implemented)
When buffer is half empty, prefetch next 32 docs into secondary buffer.

**Estimated gain**: 1-2 ns per doc (~10%)

### P7: Hybrid Approach (Recommended)
Use StreamVByte only for posting lists > 100 docs, VInt for smaller lists.

**Rationale**:
- Small lists (< 100 docs): VInt overhead is minimal, simpler is better
- Large lists (> 100 docs): StreamVByte amortizes overhead, SIMD wins
- Most queries hit both types

**Estimated gain**: 1.5-2× overall (weighted average across query mix)

## Conclusion

Buffer size optimization successfully improved StreamVByte reader performance by **1.81×**, reducing overhead from 24.8 ns to 12.4 ns per doc. However, we're still 1.59× slower than VInt due to fundamental architectural differences (batched SIMD vs inline scalar).

**Recommendation**: Consider hybrid approach (P7) for production use, or accept the tradeoff of better bulk decode performance (348 M/s raw) at the cost of higher per-call overhead.

## Related Work

- StreamVByte implementation: `docs/plans/streamvbyte_implementation.md`
- Initial integration: `docs/plans/streamvbyte_posting_list_integration.md`
- Benchmark analysis: `docs/plans/streamvbyte_benchmarks.md`
