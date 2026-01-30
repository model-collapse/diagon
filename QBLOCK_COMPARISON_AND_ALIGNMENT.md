# QBlock vs Diagon: Comparison and Alignment Plan

**Date**: 2026-01-27
**QBlock Branch**: cpwin (commit: 9c6bd44)
**Diagon Status**: Partially aligned

---

## Executive Summary

QBlock (cpp-sparse-ann) has achieved **1,174 QPS** with **90.8% recall** on MSMarco (8.8M docs), using optimized 12-bin quantization and on-demand window group allocation.

Our Diagon implementation has the basic BlockMaxQuantizedIndex structure but is **missing key optimizations** that provide 3.3x better performance.

**Recommendation**: Align Diagon with QBlock's latest cpwin branch optimizations.

---

## Performance Comparison

### QBlock Performance (cpwin branch)

| Configuration | QPS | Latency | Recall@10 | Index RAM | Memory Saved |
|---------------|-----|---------|-----------|-----------|--------------|
| **12-bin (α=0.298)** | **1,174** | **0.85 ms** | **90.8%** | **1,052 MB** | **34 MB (22.6%)** |
| 12-bin (α=0.28) | 1,113 | 0.90 ms | 89.5% | 1,052 MB | 34 MB (22.6%) |
| 32-bin (α=0.3) | 357 | 2.79 ms | 9.6% | 2,313 MB | 80 MB (19.8%) |

**Key Features**:
- **On-demand window group allocation**: 20-23% memory savings
- **12-bin custom quantization**: 3.3x faster than 32-bin
- **64-thread parallel build**: 8.1s build time
- **Optimized alpha**: 0.298 for best recall/performance balance

### Diagon Current Status

**Implemented** ✅:
- Basic BlockMaxQuantizedIndex structure
- TopKHolderOptimized (batched nth_element)
- Software prefetch in scatter-add
- Correct default parameters (window_size=500K, alpha=0.3)

**Missing** ❌:
- On-demand window group allocation (20-23% memory savings)
- 12-bin custom quantization support
- Multi-threaded parallel build
- Fine-tuned alpha parameters
- Build performance optimizations

**Expected Performance** (estimated):
- QPS: ~600-800 (with current optimizations)
- Latency: ~1.2-1.6 ms
- Recall@10: ~75-80% (with alpha=0.3)
- Index RAM: ~2.3 GB (32-bin default)

---

## Feature Gap Analysis

### 1. ❌ On-Demand Window Group Allocation (CRITICAL)

**QBlock Implementation**:
```cpp
// PASS 1: Determine maximum group_id for each term+block (lines 228-262)
std::vector<std::vector<int>> max_group_id(total_dimensions);
for (int dim = 0; dim < total_dimensions; ++dim) {
    max_group_id[dim].resize(num_quant_bins, -1);
}

// Scan all documents to find max group_id per term+block
for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
    int group_id = (doc_id / m_window_size) / m_window_group_size;
    auto view = sparse_vecs.get_sparse_vector(doc_id);
    SparseVector cut_vec = ApplyDocCutAlpha(view, doc_cut_alpha);

    for (const auto& elem : cut_vec) {
        term_t term = elem.index;
        value_t value = elem.value;
        int block_id = QuantizeWeight(value, term);

        if (block_id > 0) {
            max_group_id[term][block_id] = std::max(max_group_id[term][block_id], group_id);
        }
    }
}

// PASS 2: Allocate only needed groups (lines 264-288)
for (int dim = 0; dim < total_dimensions; ++dim) {
    m_quantized_index[dim].resize(num_quant_bins);
    for (int b = 0; b < num_quant_bins; ++b) {
        int max_grp = max_group_id[dim][b];
        if (max_grp >= 0) {
            // Allocate only up to max_grp + 1 groups
            m_quantized_index[dim][b].resize(max_grp + 1);
            allocated_groups += (max_grp + 1);
        }
        // else: leave vector empty (no groups for this term+block)
    }
}
```

**Benefits**:
- **22.6% memory savings** (12-bin config)
- **19.8% memory savings** (32-bin config)
- **Zero performance cost** (bounds check outside critical loop)

**Diagon Status**: ❌ Not implemented

**Implementation Plan**:
1. Add two-pass allocation to BlockMaxQuantizedIndex::build()
2. Pass 1: Scan docs to determine max group_id per term+block
3. Pass 2: Allocate only needed groups (resize to max_grp + 1)
4. Add bounds check in query: `if (groups.empty() || group_id >= groups.size()) continue;`

**Estimated Effort**: 4-6 hours

---

### 2. ❌ 12-Bin Custom Quantization Support

**QBlock Implementation**:
```cpp
// Uses custom LUT files (quant_one_lut.csv, quant_one_map.csv)
// 12 bins calibrated specifically for MSMarco dataset
// Better separation of important vs unimportant scores

#ifdef USE_FLOAT
int block_id = QuantizeWeight(value, term);  // Uses LUT
#else
int block_id = QuantizeWeight(uint8_t(value), term);
#endif
```

**Benefits**:
- **3.3x faster** than 32-bin (1,174 vs 357 QPS)
- **90% recall** vs 10% recall with 32-bin at similar alpha
- **54% less memory** (1,052 MB vs 2,313 MB)

**Diagon Status**: ❌ Only supports uniform quantization

**Implementation Plan**:
1. Add LUT file loading support
2. Implement custom quantization mapping (256 values → N bins)
3. Add bin count parameter (default 256, allow 12/32/64)
4. Test with provided quant_one_lut.csv / quant_one_map.csv

**Estimated Effort**: 6-8 hours

---

### 3. ❌ Multi-Threaded Parallel Build

**QBlock Implementation**:
```cpp
// Partition by window groups for parallel processing (lines 290-315)
unsigned int n_threads = std::max(1u, std::min(
    static_cast<unsigned int>(m_parameter.threads),
    std::thread::hardware_concurrency()));
int groups_per_thread = (m_num_window_groups + n_threads - 1) / n_threads;

// Thread-local block size counters
std::vector<BlockSizeIndex> thread_block_sizes(n_threads);

// Launch worker threads
std::vector<std::future<void>> futures;
for (unsigned int t = 0; t < n_threads; ++t) {
    int start_group = t * groups_per_thread;
    int end_group = std::min(start_group + groups_per_thread, m_num_window_groups);

    futures.push_back(std::async(std::launch::async, [&, t, start_group, end_group]() {
        auto& local_block_sizes = thread_block_sizes[t];

        // Process window groups [start_group, end_group)
        for (int group_id = start_group; group_id < end_group; ++group_id) {
            // Build posting lists for this group
            // ...
        }
    }));
}

// Wait for all threads
for (auto& f : futures) { f.get(); }

// Merge thread-local block sizes
for (unsigned int t = 0; t < n_threads; ++t) {
    for (int dim = 0; dim < total_dimensions; ++dim) {
        for (int b = 0; b < num_quant_bins; ++b) {
            m_block_sizes[dim][b] += thread_block_sizes[t][dim][b];
        }
    }
}
```

**Benefits**:
- **8.4x faster build** (8.1s vs 68.1s with 64 threads)
- **Scales with CPU cores** (linear scaling up to ~64 threads)
- **Production ready** (handles large datasets efficiently)

**Diagon Status**: ❌ Single-threaded only

**Implementation Plan**:
1. Partition window groups across threads
2. Use thread-local storage for block size counters
3. Merge thread-local data after parallel phase
4. Use std::async or thread pool

**Estimated Effort**: 8-10 hours

---

### 4. ❌ Optimized Alpha Parameters

**QBlock Recommendations**:
```bash
# For 90%+ recall with best performance
--alpha 0.298
--k-prime 500

# For balanced recall/speed
--alpha 0.28
--k-prime 500
```

**Performance by Alpha**:
| Alpha | QPS | Latency | Recall@10 | Use Case |
|-------|-----|---------|-----------|----------|
| 0.28 | 1,113 | 0.90 ms | 89.5% | High throughput |
| 0.298 | 1,174 | 0.85 ms | 90.8% | **Recommended** |
| 0.3 | 357 | 2.79 ms | 9.6% | Default (32-bin) |

**Diagon Status**: ✅ Default alpha=0.3, but should add presets

**Implementation Plan**:
1. Add alpha presets: HIGH_RECALL=0.298, BALANCED=0.28, FAST=0.2
2. Update documentation with recommended values
3. Add k' optimization (use 500 instead of 1000 for faster queries)

**Estimated Effort**: 2-3 hours

---

### 5. ✅ TopKHolderOptimized (ALREADY IMPLEMENTED)

**Status**: ✅ **Already aligned** with QBlock

- Batched nth_element for O(N) amortized complexity
- Accumulates 3K candidates before partial sort
- Threshold-based pruning

**Files**:
- `src/core/include/diagon/index/TopKHolderOptimized.h`
- Used in BlockMaxQuantizedIndex.cpp

---

### 6. ✅ Software Prefetch (ALREADY IMPLEMENTED)

**Status**: ✅ **Already aligned** with QBlock

- Prefetch 48 cache lines ahead
- Pattern: initial prefetch + loop prefetch + tail
- 20-30% speedup in scatter-add phase

**Code**:
```cpp
constexpr size_t kPrefetchDistance = 48;

// Initial prefetch
for (size_t p = 0; p < pf_count; ++p) {
    __builtin_prefetch(&score_buf[docs[p]], 1, 0);
}

// Main loop with prefetch
for (; j + kPrefetchDistance < n; ++j) {
    __builtin_prefetch(&score_buf[docs[j + kPrefetchDistance]], 1, 0);
    // Process docs[j]
}

// Tail loop
for (; j < n; ++j) {
    // Process remaining docs
}
```

---

### 7. ✅ Correct Default Parameters (ALREADY IMPLEMENTED)

**Status**: ✅ **Already aligned** with QBlock

- window_size = 500,000 (0.5M for normal CPU)
- alpha = 0.3 (balanced recall/speed)

**Files**:
- `src/core/include/diagon/index/BlockMaxQuantizedIndex.h` (lines 57, 69)

---

## Implementation Priority

### Phase 1: Critical Optimizations (Must Have)

1. **On-Demand Window Group Allocation** (4-6 hours)
   - Priority: **CRITICAL**
   - Impact: 20-23% memory savings with zero performance cost
   - Complexity: Medium
   - **ROI**: Very high (easy win)

2. **12-Bin Custom Quantization** (6-8 hours)
   - Priority: **HIGH**
   - Impact: 3.3x performance improvement
   - Complexity: Medium
   - **ROI**: Extremely high (biggest impact)

### Phase 2: Performance Enhancements (Should Have)

3. **Multi-Threaded Parallel Build** (8-10 hours)
   - Priority: **MEDIUM**
   - Impact: 8.4x faster index build time
   - Complexity: High
   - **ROI**: High (production scalability)

4. **Optimized Alpha Parameters** (2-3 hours)
   - Priority: **LOW**
   - Impact: Better recall/latency balance
   - Complexity: Low
   - **ROI**: Medium (tuning)

**Total Estimated Effort**: 20-27 hours

---

## Benchmarking Plan

### Test Configuration

**Dataset**: MSMarco v1 SPLADE (8.8M docs)
- Documents: `Datasets/msmarco_v1_splade/docs.csr`
- Queries: `Datasets/msmarco_v1_splade/queries.csr` (6,980 queries)
- Ground Truth: `Datasets/msmarco_v1_splade/cocondense_ground_truth_int.txt`

**Parameters**:
```bash
# After alignment, test with:
--alpha 0.298
--k-prime 500
--top-k 10
--window-group-size 15
--lut-file quant_one_lut.csv
--quant-map-file quant_one_map.csv
```

**Expected Results** (after all optimizations):
- QPS: 1,100-1,200 (match QBlock)
- Latency: 0.85-0.90 ms (match QBlock)
- Recall@10: 90%+ (match QBlock)
- Index RAM: ~1.0 GB (match QBlock)
- Memory saved: 20-23% (match QBlock)

---

## Key Learnings from QBlock

### 1. Two-Pass Allocation is Essential

QBlock's on-demand allocation uses a smart two-pass approach:
- **Pass 1**: Scan all docs to determine sparsity (which groups are actually needed)
- **Pass 2**: Allocate only non-empty groups

This is **far better** than pre-allocating all groups and marking empties.

### 2. Custom Quantization Matters More Than Bins

12-bin custom quantization **outperforms** 32-bin default by 3.3x because:
- LUT is calibrated specifically for MSMarco score distribution
- Better separation between important and unimportant scores
- Fewer bins = less memory + faster processing

**Lesson**: Domain-specific quantization > generic uniform quantization

### 3. Parallel Build is Production-Critical

Single-threaded build:
- 68s for 8.8M docs
- Acceptable for research, **unacceptable for production**

Multi-threaded build (64 threads):
- 8.1s for 8.8M docs
- **8.4x speedup**
- Essential for real-time index updates

### 4. Alpha Tuning is Dataset-Specific

Default alpha=0.3 works poorly with 32-bin:
- Recall@10: 9.6% (terrible)
- QPS: 357 (slow)

Optimized alpha=0.298 with 12-bin:
- Recall@10: 90.8% (excellent)
- QPS: 1,174 (very fast)

**Lesson**: Must tune alpha for each dataset + quantization configuration

---

## Next Steps

### Immediate Actions

1. **Clone QBlock quantization files**:
```bash
cp /home/ubuntu/cpp-sparse-ann/quant_one_lut.csv /home/ubuntu/diagon/benchmarks/
cp /home/ubuntu/cpp-sparse-ann/quant_one_map.csv /home/ubuntu/diagon/benchmarks/
```

2. **Implement on-demand allocation** (Phase 1, Item 1)
   - Highest ROI
   - Simplest to implement
   - Immediate 20% memory savings

3. **Add 12-bin quantization support** (Phase 1, Item 2)
   - Biggest performance impact
   - Requires LUT file loading
   - Test with provided LUT files

4. **Benchmark and validate**:
   - Compare against QBlock results
   - Verify 90%+ recall
   - Measure QPS and latency

### Future Work

5. **Implement parallel build** (Phase 2, Item 3)
   - Production scalability
   - Linear scaling with cores
   - Test with 16/32/64 threads

6. **Add alpha presets** (Phase 2, Item 4)
   - HIGH_RECALL, BALANCED, FAST
   - Document recommended values
   - Update examples

---

## References

**QBlock Repository**:
- URL: https://github.com/model-collapse/cpp-sparse-ann
- Branch: cpwin (commit: 9c6bd44)
- Key files:
  - `cpp/src/BitQIndex.cpp` (lines 215-400: on-demand allocation)
  - `cpp/src/topk_optimized.h` (TopKHolderOptimized)
  - `quant_one_lut.csv` (12-bin quantization LUT)
  - `quant_one_map.csv` (256 → 12 bin mapping)

**QBlock Documentation**:
- `README.md` (recommended configuration)
- `BENCHMARK_RESULTS.md` (full benchmark data)
- `FINAL_COMPARISON.md` (12-bin vs 32-bin comparison)
- `ON_DEMAND_ALLOCATION_SUMMARY.md` (implementation details)

**Diagon Current Status**:
- Repository: /home/ubuntu/diagon
- Key file: `src/core/src/index/BlockMaxQuantizedIndex.cpp`
- Documentation: `QBLOCK_IMPLEMENTATION_FIXES.md`

---

**Prepared By**: Claude Sonnet 4.5
**Date**: 2026-01-27
**Status**: Ready for implementation
**Estimated Timeline**: 20-27 hours total effort
