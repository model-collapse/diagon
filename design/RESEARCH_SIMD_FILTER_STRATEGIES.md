# Research: Optimal Filtering Strategies for SIMD Scatter-Add

## Problem Statement

When combining filtering with SIMD scatter-add scoring, there are two main approaches:

### Approach 1: List Merge Scanning (Current Lucene/Traditional)
```cpp
// Step 1: Apply filter to get matching docs
DocIdSet filteredDocs = filter->getDocIdSet(context);
BitSet* bits = filteredDocs.getBitSet();

// Step 2: Extract doc IDs that pass filter
vector<int> matchingDocs;
for (int doc = bits->nextSetBit(0); doc != -1;
     doc = bits->nextSetBit(doc + 1)) {
    matchingDocs.push_back(doc);
}

// Step 3: SIMD scatter-add only on filtered docs
for (const auto& term : queryTerms) {
    auto* tfColumn = getTfColumn(term);
    tfColumn->simdScatterAdd(term.weight, matchingDocs, scores);
}
```

**Characteristics**:
- ✅ Process only filtered docs
- ✅ Skip docs that fail filter
- ❌ BitSet scanning has branches (nextSetBit loop)
- ❌ Extracting doc IDs has overhead
- ❌ Gather operations on sparse doc IDs (potential cache misses)

---

### Approach 2: Pre-Fill Score Buffer (Branchless SIMD)
```cpp
// Step 1: Initialize all scores to sentinel value
vector<float> scores(maxDoc, -INFINITY);

// Step 2: Set filtered-in docs to 0 (or filtered-out to -∞)
DocIdSet filteredDocs = filter->getDocIdSet(context);
if (filteredDocs.isDense()) {
    // Use BitSet: vectorized mask operation
    BitSet* bits = filteredDocs.getBitSet();
    for (int i = 0; i < maxDoc; i += 8) {
        __m256 mask = _mm256_loadu_ps(&bits->words[i/64]);
        __m256 zero = _mm256_setzero_ps();
        _mm256_maskstore_ps(&scores[i], mask, zero);
    }
} else {
    // Sparse: iterate and set
    for (int doc : filteredDocs.getDocIds()) {
        scores[doc] = 0.0f;
    }
}

// Step 3: SIMD scatter-add on ALL docs (branchless)
for (const auto& term : queryTerms) {
    auto* tfColumn = getTfColumn(term);
    tfColumn->simdScatterAddAll(term.weight, scores);
    // -∞ + anything = -∞, so filtered docs stay -∞
}

// Step 4: Filter out -∞ scores
TopDocs results = heapSelectFinite(scores, topK);
```

**Characteristics**:
- ✅ Full SIMD utilization (no branching in main loop)
- ✅ Sequential memory access in scatter-add
- ❌ Process docs that will be filtered out
- ❌ Initialization overhead (write maxDoc floats)
- ✅ Arithmetic on -∞ is branchless

---

## Cost Model Analysis

### Parameters
- `N` = total number of documents (e.g., 100,000 in a window)
- `M` = number of docs passing filter (selectivity × N)
- `T` = number of query terms
- `W` = SIMD width (8 for AVX2, 16 for AVX-512)
- `selectivity` = M/N (fraction of docs passing filter)

### Approach 1 Cost: List Merge Scanning

**Step 1: Extract doc IDs from BitSet**
- Scan BitSet: O(N) bitwise operations
- Branch on each set bit: M branches (potential mispredictions)
- Memory bandwidth: N/8 bytes (BitSet) + M×4 bytes (doc IDs)
- **Time**: `extract_cost = (N/64) + M × branch_penalty`
  - Assume branch_penalty ≈ 5 cycles (if unpredictable)
  - For N=100K, M=10K: (100K/64) + 10K×5 ≈ 1,562 + 50,000 = **51,562 cycles**

**Step 2: SIMD scatter-add on filtered docs**
- For each term, gather TF values for M docs
- Gather operation: potentially random access
- SIMD multiply-add: M/W operations per term
- **Time**: `scatter_cost_filtered = T × (M/W × simd_cycle + M × gather_cost)`
  - Assume simd_cycle ≈ 1, gather_cost ≈ 10 cycles (cache miss)
  - For T=3, M=10K, W=8: 3 × (10K/8 × 1 + 10K × 10) = 3 × (1,250 + 100,000) = **303,750 cycles**

**Total**: 51,562 + 303,750 ≈ **355K cycles**

---

### Approach 2 Cost: Pre-Fill Score Buffer

**Step 1: Initialize score buffer to -∞**
- Sequential write: N floats
- Highly optimized (streaming stores)
- **Time**: `init_cost = N/W × simd_cycle`
  - For N=100K, W=8: 100K/8 × 1 = **12,500 cycles**

**Step 2: Set filtered docs to 0**
- **Dense filter (high selectivity)**: Vectorized mask store
  - **Time**: `mask_cost = N/W × simd_cycle` = **12,500 cycles**
- **Sparse filter (low selectivity)**: Iterate M docs
  - **Time**: `sparse_set_cost = M × write_cycle` ≈ M cycles
  - For M=10K: **10,000 cycles**

**Step 3: SIMD scatter-add on ALL docs**
- Sequential access to TF columns
- No gather (sequential processing)
- **Time**: `scatter_cost_all = T × (N/W × simd_cycle)`
  - For T=3, N=100K, W=8: 3 × (100K/8 × 1) = 3 × 12,500 = **37,500 cycles**

**Step 4: Filter finite scores and heap select**
- Scan scores array: N/W SIMD comparisons
- Build heap: O(K log K) where K = topK
- **Time**: `filter_cost = N/W + K×log(K)`
  - For N=100K, K=100, W=8: 12,500 + 664 = **13,164 cycles**

**Total (sparse filter)**: 12,500 + 10,000 + 37,500 + 13,164 = **73,164 cycles**
**Total (dense filter)**: 12,500 + 12,500 + 37,500 + 13,164 = **75,664 cycles**

---

## Performance Comparison

### Scenario 1: Low Selectivity (10% pass filter)
- N=100K, M=10K, T=3, W=8
- **Approach 1**: 355K cycles
- **Approach 2**: 73K cycles
- **Speedup**: **4.9×** ✅

### Scenario 2: Medium Selectivity (50% pass filter)
- N=100K, M=50K, T=3, W=8
- **Approach 1**:
  - Extract: 1,562 + 50K×5 = 251,562 cycles
  - Scatter: 3 × (50K/8 + 50K×10) = 3 × 518,750 = 1,556,250 cycles
  - **Total**: **1,807,812 cycles**
- **Approach 2**:
  - Init: 12,500
  - Set: 50,000
  - Scatter: 37,500
  - Filter: 13,164
  - **Total**: **113,164 cycles**
- **Speedup**: **16×** ✅✅

### Scenario 3: High Selectivity (90% pass filter)
- N=100K, M=90K, T=3, W=8
- **Approach 1**:
  - Extract: 1,562 + 90K×5 = 451,562 cycles
  - Scatter: 3 × (90K/8 + 90K×10) = 3 × 911,250 = 2,733,750 cycles
  - **Total**: **3,185,312 cycles**
- **Approach 2**:
  - Init: 12,500
  - Set: 90,000 (or use dense mask: 12,500)
  - Scatter: 37,500
  - Filter: 13,164
  - **Total**: **153,164 cycles** (sparse) or **75,664 cycles** (dense)
- **Speedup**: **20.8×** (sparse) or **42×** (dense) ✅✅✅

### Scenario 4: Very Low Selectivity (1% pass filter)
- N=100K, M=1K, T=3, W=8
- **Approach 1**:
  - Extract: 1,562 + 1K×5 = 6,562 cycles
  - Scatter: 3 × (1K/8 + 1K×10) = 3 × 10,125 = 30,375 cycles
  - **Total**: **36,937 cycles**
- **Approach 2**:
  - Init: 12,500
  - Set: 1,000
  - Scatter: 37,500
  - Filter: 13,164
  - **Total**: **64,164 cycles**
- **Speedup**: **0.58×** ❌ (Approach 1 wins!)

---

## Key Insights

### 1. Crossover Point
**Pre-fill wins when**: `selectivity < ~5%` is FALSE!

Actually, **pre-fill wins almost always** except at **extremely low selectivity** (<1-2%).

Why? Because gather operations dominate Approach 1's cost:
- Gather cost per doc: ~10 cycles (random access)
- Sequential SIMD cost per doc: ~1/W cycles
- Ratio: **80× difference** for W=8

### 2. The Gather Problem
Approach 1 suffers from:
- Random memory access patterns (doc IDs not contiguous)
- Cache misses on TF column lookups
- Cannot utilize hardware prefetchers

Approach 2 benefits from:
- Sequential access to TF columns
- Predictable memory patterns
- Hardware prefetcher friendly

### 3. Infinity Arithmetic
Using -∞ as sentinel is critical:
- `-∞ + x = -∞` (branchless)
- `-∞ < any_finite` (easy to filter)
- No NaN propagation issues

### 4. Dense vs Sparse Filter Initialization
For high selectivity (>50%):
- Use **dense mask**: vectorized write with mask
- For low selectivity (<50%):
- Use **sparse iteration**: write only M docs

---

## Proposed Dynamic Selection Algorithm

```cpp
enum class FilterStrategy {
    LIST_MERGE,      // Traditional: extract then scatter
    PREFILL_SPARSE,  // Pre-fill -∞, set M docs to 0
    PREFILL_DENSE    // Pre-fill -∞, masked write for passed docs
};

FilterStrategy selectFilterStrategy(
    int numDocs,
    float selectivity,
    int numQueryTerms,
    int simdWidth) {

    // Estimate costs
    int M = static_cast<int>(numDocs * selectivity);

    // Cost for list merge (dominated by gather)
    double listMergeCost =
        (numDocs / 64.0) +              // BitSet scan
        M * 5.0 +                        // Branch penalty
        numQueryTerms * M * 10.0;        // Gather cost

    // Cost for pre-fill sparse
    double prefillSparseCost =
        numDocs / simdWidth +            // Init to -∞
        M +                              // Set filtered to 0
        numQueryTerms * numDocs / simdWidth +  // Sequential scatter
        numDocs / simdWidth;             // Filter finite

    // Cost for pre-fill dense (if high selectivity)
    double prefillDenseCost =
        numDocs / simdWidth +            // Init to -∞
        numDocs / simdWidth +            // Masked write
        numQueryTerms * numDocs / simdWidth +  // Sequential scatter
        numDocs / simdWidth;             // Filter finite

    // Decision boundary
    if (selectivity < 0.01) {
        // Very low selectivity: list merge might win
        if (listMergeCost < prefillSparseCost) {
            return FilterStrategy::LIST_MERGE;
        }
    }

    // Choose between sparse and dense pre-fill
    if (selectivity > 0.5) {
        return FilterStrategy::PREFILL_DENSE;
    } else {
        return FilterStrategy::PREFILL_SPARSE;
    }
}
```

---

## Additional Optimizations

### 1. Hybrid Approach for Multi-Level Filters
```cpp
// Step 1: Use skip indexes to prune granules (coarse)
vector<int> candidateGranules = skipIndex->filterGranules(filter);

// Step 2: Within each granule, use pre-fill strategy (fine)
for (int granuleId : candidateGranules) {
    vector<float> granuleScores(8192, -INFINITY);

    // Fine-grained filter within granule
    applyFineGrainedFilter(granuleId, filter, granuleScores);

    // SIMD scatter-add on granule
    for (const auto& term : queryTerms) {
        simdScatterAddGranule(term, granuleId, granuleScores);
    }

    mergeTopK(granuleScores, globalTopK);
}
```

### 2. Filter Fusion
When multiple filters are ANDed:
```cpp
// Option A: Apply filters sequentially (multiple passes)
scores = initToInfinity();
filter1->applySIMD(scores);  // Set non-matching to -∞
filter2->applySIMD(scores);  // Set non-matching to -∞
// ... then scatter-add

// Option B: Fuse filters into single SIMD pass
scores = initToInfinity();
fusedFilter->applySIMD(scores);  // Combined mask
// ... then scatter-add
```

### 3. Adaptive Granule Processing
```cpp
// If a granule has very low selectivity after skip index
if (estimatedSelectivity < 0.01) {
    // Use list merge for this granule
    processGranuleListMerge(granule, filter);
} else {
    // Use pre-fill for this granule
    processGranulePrefill(granule, filter);
}
```

---

## Experimental Validation Needed

### Benchmarks to Run (PoC Phase)

1. **Vary Selectivity** (0.1%, 1%, 5%, 10%, 25%, 50%, 75%, 90%)
   - Measure actual cycles/latency
   - Validate cost model
   - Find empirical crossover point

2. **Vary Number of Terms** (1, 2, 3, 5, 10, 20)
   - More terms → more benefit from pre-fill
   - Measure scaling

3. **Vary Window Size** (10K, 50K, 100K, 500K, 1M)
   - Cache effects
   - TLB effects

4. **Different Filter Types**
   - Range filter (price > 50 AND price < 200)
   - Term filter (category = "electronics")
   - Bloom filter (probabilistic)
   - Composite (AND/OR of multiple filters)

5. **Hardware Variations**
   - AVX2 (256-bit, 8-wide)
   - AVX-512 (512-bit, 16-wide)
   - Different CPUs (Skylake, Ice Lake, Zen 3, Zen 4)

### Metrics to Collect

- **Cycles per query**
- **Instructions per query**
- **Cache misses** (L1, L2, L3)
- **Branch mispredictions**
- **Memory bandwidth utilization**
- **SIMD instruction mix** (gather vs sequential load)

---

## Recommended Implementation Strategy

### Phase 1: Implement Both Strategies
```cpp
class FilteredSIMDScorer {
public:
    TopDocs score(
        const Query& query,
        const Filter& filter,
        int topK) {

        float selectivity = estimateSelectivity(filter);
        FilterStrategy strategy = selectFilterStrategy(
            numDocs_, selectivity, query.numTerms(), simdWidth_);

        switch (strategy) {
            case FilterStrategy::LIST_MERGE:
                return scoreListMerge(query, filter, topK);
            case FilterStrategy::PREFILL_SPARSE:
                return scorePrefillSparse(query, filter, topK);
            case FilterStrategy::PREFILL_DENSE:
                return scorePrefillDense(query, filter, topK);
        }
    }

private:
    TopDocs scoreListMerge(...);      // Approach 1
    TopDocs scorePrefillSparse(...);  // Approach 2 (sparse)
    TopDocs scorePrefillDense(...);   // Approach 2 (dense)

    float estimateSelectivity(const Filter& filter) {
        // Use skip index statistics
        // Or filter->estimateSelectivity()
        // Or maintain statistics from previous queries
    }
};
```

### Phase 2: Collect Runtime Statistics
```cpp
struct FilterStrategyStats {
    FilterStrategy strategy;
    float selectivity;
    int numTerms;
    uint64_t cycles;
    uint64_t l3_misses;
};

class AdaptiveFilteredScorer {
    // Collect statistics during execution
    void recordStats(const FilterStrategyStats& stats) {
        stats_.push_back(stats);
    }

    // Periodically retrain decision model
    void retrainModel() {
        // Use collected stats to refine crossover points
        // Machine learning or simple threshold tuning
    }
};
```

### Phase 3: Refinement
- Tune decision boundaries based on actual measurements
- Consider additional factors (query complexity, concurrent queries, etc.)
- Add more sophisticated cost models

---

## Open Questions for PoC

1. **What is the actual gather cost?**
   - Highly dependent on TF column layout
   - Cache state (cold vs warm)
   - Prefetcher effectiveness

2. **Does -∞ behave optimally?**
   - FP exception overhead?
   - Subnormal number handling?
   - Alternative: use `NaN` or `INT_MIN`?

3. **Filter result caching interaction?**
   - If filter results are cached, does it change decision?
   - Cached BitSet extraction might be faster

4. **Multi-core scaling?**
   - Does pre-fill approach scale better with more cores?
   - Memory bandwidth contention?

5. **Compiler optimizations?**
   - Does compiler auto-vectorize pre-fill approach?
   - Are intrinsics necessary?

---

## Conclusion

**Hypothesis**: Pre-fill approach will win for **most realistic scenarios** (selectivity > 1-2%) due to:
- Elimination of expensive gather operations
- Sequential memory access patterns
- Full SIMD utilization without branching

**Crossover point**: Likely around **0.5-2% selectivity**, but needs empirical validation.

**Recommendation**:
1. Implement both strategies
2. Use dynamic selection based on estimated selectivity
3. Collect runtime statistics during PoC
4. Refine decision boundary with actual measurements
5. Consider hybrid approach with granule-level adaptation

**Next Steps**:
- Add filter strategy selection to Module 14 design
- Update `UnifiedSIMDQueryProcessor` with adaptive selection logic
- Document decision model and tuning parameters
