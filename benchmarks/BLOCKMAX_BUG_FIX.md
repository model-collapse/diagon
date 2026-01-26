# BlockMaxQuantizedIndex Bug Fix: uint8_t Overflow

## Problem

The BlockMaxQuantizedIndex implementation had a critical bug causing an infinite loop during queries.

### Symptoms
- Benchmark would hang during the query phase
- Build phase completed successfully
- Process would print "Querying with alpha = 0.3..." but never progress
- Eventually exhausted memory and crashed with `std::bad_alloc`

### Root Cause

**Integer Overflow in Loop Counter**

Location: `BlockMaxQuantizedIndex.cpp` line ~113

```cpp
// BUGGY CODE:
for (uint8_t block_id = 0; block_id < config_.num_quantization_bins; ++block_id) {
    // Process block
}
```

**Problem**:
- `block_id` was declared as `uint8_t` (8-bit unsigned integer: range 0-255)
- `config_.num_quantization_bins` was set to 256
- When `block_id` reached 255 and incremented, it wrapped around to 0 instead of 256
- The loop condition `block_id < 256` remained true forever
- Result: **INFINITE LOOP**

### Debug Output

When instrumented with logging, the output showed:

```
Processing query term 0/28, term=1055
  Starting block loop...
  Block 0/256
  Block 50/256
  Block 100/256
  Block 150/256
  Block 200/256
  Block 250/256
  Block 0/256    <-- WRAPPED BACK TO 0!
  Block 50/256
  Block 100/256
  Block 150/256
  Block 200/256
  Block 250/256
  Block 0/256
  ... (repeats forever)
```

## Solution

Changed loop counter from `uint8_t` to `size_t`:

```cpp
// FIXED CODE:
for (size_t block_id = 0; block_id < config_.num_quantization_bins; ++block_id) {
    uint32_t block_size = block_sizes_[term][block_id];

    if (block_size > 0) {
        float block_max_score = dequantizeScore(static_cast<uint8_t>(block_id));
        float gain = block_max_score * q_weight;

        blocks_with_score.emplace_back(
            term, static_cast<uint8_t>(block_id), gain, &quantized_index_[term][block_id]
        );
    }
}
```

**Changes**:
1. Loop counter: `uint8_t block_id` → `size_t block_id`
2. Cast when passing to functions: `static_cast<uint8_t>(block_id)`
3. Now the loop correctly exits when `block_id` reaches 256

## Results After Fix

The benchmark now completes successfully!

### Build Performance (10K documents):
- Build time: 554.569 ms
- Throughput: **18,032 docs/sec**
- Memory: 43.6 MB

### Query Performance (100 queries):

| Alpha | QPS     | Latency (ms) | Recall@10 | Blocks | Score Ops |
|-------|---------|--------------|-----------|--------|-----------|
| 0.3   | 2994.07 | 0.33         | 0.20%     | 140    | 297       |
| 0.5   | 2842.01 | 0.35         | 0.20%     | 328    | 836       |
| 0.7   | 2547.05 | 0.39         | 0.20%     | 673    | 2,001     |
| 1.0   | 1408.25 | 0.71         | 0.20%     | 3,399  | 17,725    |

**Note**: Low recall (0.2%) is expected because we only indexed 10K documents out of 8.8M in the dataset, so most ground truth documents are not in the index.

## Lessons Learned

### 1. **Beware of Unsigned Integer Wraparound**
- `uint8_t` max value is 255
- Incrementing 255 wraps to 0, not 256
- Always check loop conditions with small integer types

### 2. **Use Appropriate Loop Counter Types**
- For loop counters that need to exceed 255, use `int`, `size_t`, or `uint32_t`
- `uint8_t` should only be used for data storage, not loop iteration

### 3. **Common Pattern in Lucene/Search Engines**
- Quantization bins are often 256 (to fit in uint8_t for storage)
- But loops over 0-255 inclusive need larger counter types

### 4. **Debug Strategies That Worked**
- Progressive enablement: Test each phase separately
- Detailed logging at loop boundaries
- Timeout to prevent hung processes

## Comparison with QBlock

After fixing the bug, DIAGON's performance is:

**Build Speed**:
- DIAGON: 18,032 docs/sec (single-threaded)
- QBlock: 985,726 docs/sec (64 threads + SIMD)
- Gap: **54x slower** (expected due to no parallelization or SIMD)

**Query Speed** (alpha=0.5):
- DIAGON: 2,842 QPS
- QBlock: 264 QPS
- Result: DIAGON is **10.7x faster!**

**Why is DIAGON query faster?**
- Small dataset (10K docs) fits in cache
- No disk I/O overhead
- Optimized two-pointer sparse dot product
- Min-heap for top-k candidate management

**Note**: On QBlock's full 8.8M document dataset, QBlock would be much faster due to:
- Better cache utilization with window-based processing
- SIMD scatter-add operations
- Prefetching

## File: BlockMaxQuantizedIndex.cpp

**Fixed lines**: 113-126

**Commit message**:
```
Fix infinite loop in BlockMaxQuantizedIndex due to uint8_t overflow

Loop counter for iterating over 256 quantization bins was using uint8_t,
which wraps from 255 to 0 instead of reaching 256. Changed to size_t
with explicit casts when passing to functions that expect uint8_t.

This bug caused queries to hang indefinitely and eventually crash with
std::bad_alloc due to memory exhaustion.
```

---

**Date**: 2026-01-26
**Bug Severity**: Critical (Infinite Loop)
**Impact**: Complete query failure
**Fix Complexity**: Trivial (1 line change)
**Detection Method**: Debug logging + timeout
