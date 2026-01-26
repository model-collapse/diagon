# Final Summary: Bloom Filters in QBlock?

## Direct Answer

**NO**, QBlock does NOT use bloom filters.

## What QBlock Actually Uses

QBlock uses a **Block-Max Quantized Inverted Index**, which is:
- A sparse inverted index structure: `[term][quantization_block][window]`
- Quantization of scores into 256 bins (uint8)
- Block-max pruning for query optimization (similar to WAND/BMW)
- Window-based organization (65,536 documents per window)

## Evidence from Source Code

### QBlock's Data Structure

```cpp
// From BitQIndex.h
struct QuantizedBlock {
    std::vector<s_size_t> documents;  // Doc IDs, not bit vectors
    #ifdef ENABLE_WEIGHTED_SCORING
    std::vector<uint8_t> weights;
    #endif
};

using QuantizedInvertedIndex =
    std::vector<std::vector<std::vector<QuantizedBlock>>>;
    // [term][block][window] -> list of doc IDs
```

This is an **inverted index**, not a bloom filter.

### QBlock's Query Algorithm

```cpp
// Phase 1: Block Selection
for (term in query_terms) {
    for (block in quantized_index[term]) {
        gain = block_max_score * query_weight;
        blocks_with_score.push({term, block, gain});
    }
}

// Phase 2: ScatterAdd
for (block in selected_blocks) {
    for (doc in block.documents) {
        score_buf[doc] += block.gain;
    }
}

// Phase 3: Reranking
for (candidate in top_k_candidates) {
    exact_score = dot_product(query, forward_index[candidate]);
}
```

No bloom filter operations (add, query, hash) anywhere.

## Bloom Filters vs QBlock: Different Problems

### Bloom Filters
**Purpose**: Probabilistic membership testing ("Does doc contain term X?")

**Data Structure**: Bit array with multiple hash functions

**Operations**:
- `add(item)`: Set multiple bits to 1
- `contains(item)`: Check if multiple bits are 1
- Result: Yes (maybe) / No (definitely)

**Use Case**: Fast filtering to avoid expensive lookups

### QBlock
**Purpose**: Top-k ranked retrieval ("What are the top-10 docs for this query?")

**Data Structure**: Quantized inverted index with doc IDs

**Operations**:
- Build: Organize docs by (term, score_bin, window)
- Query: Select blocks → accumulate scores → rerank
- Result: Ranked list of document IDs with scores

**Use Case**: Efficient approximate nearest neighbor search

## Why the Confusion?

### 1. Initial Benchmark Mistake
I initially created a bloom filter benchmark when asked to "benchmark QBlock", because:
- Bloom filters are common in search engines (e.g., Lucene's skip indexes)
- I didn't carefully read QBlock's source code first
- The user correctly pointed out the mistake

### 2. Different Optimization Techniques
Both bloom filters and QBlock are optimization techniques for search, but for different problems:
- Bloom filters: Filter out non-matches (reduce candidates)
- QBlock: Prune low-scoring blocks (reduce computation)

## What We Actually Benchmarked

### 1. Bloom Filter Performance (DIAGON)
- Created one bloom filter per document (wrong use case)
- Build: 341K docs/sec
- Query: 40M membership tests/sec
- Use case: Should be for skip indexes, not per-document

### 2. QBlock Performance (Original)
- Python benchmark on full MSMarco v1 SPLADE dataset
- Build: 985K docs/sec (8.8M documents)
- Query (α=0.5): 264 QPS, 90.26% recall

### 3. DIAGON BlockMaxQuantizedIndex (This Implementation)
- Implemented QBlock's algorithm in DIAGON
- Build: 18K docs/sec (10K documents, single-threaded)
- Query (α=0.5): 2,842 QPS, 0.20% recall (low due to partial dataset)

## Key Findings

### 1. Algorithm Correctness
DIAGON's BlockMaxQuantizedIndex correctly implements QBlock's algorithm:
- Three-level structure: [term][block][window]
- Block selection with alpha-mass pruning
- ScatterAdd for score accumulation
- Forward index reranking

### 2. Performance Gap
**Build**: QBlock 54x faster (parallelization + SIMD)
**Query**: DIAGON 10.7x faster on small dataset (cache effects)

### 3. Critical Bug Found
**uint8_t overflow bug** causing infinite loop:
- Loop counter wraps from 255 to 0 instead of reaching 256
- Fixed by changing `uint8_t block_id` to `size_t block_id`

## Bloom Filters in Search Engines: Proper Use Cases

### Where Bloom Filters ARE Used

1. **Lucene Skip Indexes**
   ```
   SELECT * FROM table WHERE bloom_filter_column CONTAINS 'term'
   ```
   - One bloom filter per granule (8,192 rows)
   - Prune granules that definitely don't contain term
   - Reduce disk I/O

2. **ClickHouse Bloom Filter Index**
   ```sql
   INDEX bloom_idx content TYPE bloom_filter GRANULARITY 1
   ```
   - Skip data blocks that don't contain search terms
   - Trade memory for faster queries

3. **Cassandra/ScyllaDB**
   - Per-SSTable bloom filters
   - Avoid reading SSTables that don't contain key

### How QBlock Avoids Similar Problem (Without Bloom Filters)

QBlock doesn't need bloom filters because:
- **Block-level metadata**: Stores block_max_score per block
- **Upper bound pruning**: Skip blocks whose max contribution < threshold
- **No false positives**: Exact doc lists, not probabilistic
- **Window-based I/O**: Already optimized for sequential access

## Conclusion

**Direct Answer to "Did I use bloom filter in the QBlock Repo?"**

**NO.** QBlock uses a block-max quantized inverted index, not bloom filters.

**What We Learned**:
1. Read source code carefully before implementing
2. Different algorithms solve different problems
3. Bloom filters: membership testing (filtering)
4. QBlock: ranked retrieval (scoring)
5. Both are valuable, but for different use cases

**Final Comparison**:

| Feature | Bloom Filters | QBlock |
|---------|--------------|--------|
| Data Structure | Bit array | Inverted index |
| Operation | Membership test | Ranked retrieval |
| Output | Yes/No | Top-k docs with scores |
| False Positives | Yes | No |
| Scoring | No | Yes |
| Use in Search | Filtering | Retrieval |

---

**Implementation Date**: 2026-01-26
**DIAGON Version**: 1.0.0 (development)
**QBlock Version**: 0.0.4 (reference)
