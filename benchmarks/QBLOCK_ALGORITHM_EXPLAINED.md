# QBlock BitQ Algorithm Explained

## Overview

After analyzing the QBlock codebase, here's what **BitQIndex** actually does:

**BitQ is a Block-Max Quantized Inverted Index** for approximate sparse vector retrieval.

---

## Core Data Structure

### 1. Quantized Inverted Index

```cpp
// Three-level structure: [term][block][window]
using QuantizedInvertedIndex = std::vector<std::vector<std::vector<QuantizedBlock>>>;

// First dimension: term (vocabulary)
// Second dimension: quantization block (value buckets)
// Third dimension: window (document chunks)
```

**Key insight**: This is NOT a per-document bloom filter! It's an **inverted index** with **quantized scores** organized into **blocks**.

### 2. Quantization Blocks

Documents are not indexed individually. Instead, they're grouped by:

**Quantization bins**: Float scores → 256 bins (uint8)
```cpp
int block_id = QuantizeWeight(doc_score, term);  // Maps score to bin
m_quantized_index[term][block_id][window_id].documents.push_back(doc_id);
```

For each term, documents are split across ~36-256 blocks based on their score for that term.

### 3. Window Structure

```cpp
m_window_size = 65536;  // 64K documents per window
m_num_windows = (num_docs + m_window_size - 1) / m_window_size;
```

Documents are chunked into windows for memory locality and parallel processing.

---

## Index Construction Algorithm

### Phase 1: Quantize Documents

```cpp
for (each document) {
    for (each term in document with score) {
        block_id = quantize(score);  // Map score to block (0-255)
        window_id = doc_id / 65536;

        // Add to inverted index
        m_quantized_index[term][block_id][window_id].documents.push_back(doc_id);
    }
}
```

**Result**: Inverted index where postings are organized by score buckets.

### Phase 2: Build Block Statistics

```cpp
for (each term) {
    for (each block) {
        m_block_sizes[term][block] = count(documents in block);
    }
}
```

---

## Query Algorithm

### Step 1: Block Selection (Pruning)

```cpp
vector<BlockWithScore> blocks_with_score;

// For each query term
for (term in query) {
    for (block in m_quantized_index[term]) {
        // Calculate block's contribution
        gain = quant_value[block] * query_term_weight;

        if (block has documents) {
            blocks_with_score.push_back({term, block, gain});
        }
    }
}

// Prune blocks based on alpha parameter
SelectBlocks(blocks_with_score, alpha);
```

**Alpha selection strategies**:
- **Alpha-mass**: Select blocks contributing top alpha% of total score mass
- **Max-ratio**: Select blocks with gain ≥ alpha × max_gain

**This is block-max pruning**, similar to:
- WAND (Weak AND)
- Block-Max WAND
- BMW (Block-Max WAND)

### Step 2: ScatterAdd (Score Accumulation)

```cpp
vector<int32_t> score_buf(window_size, 0);  // Accumulator per window

for (each window) {
    // Part 1: Accumulate scores
    for (selected_block) {
        for (doc in block.documents) {
            score_buf[doc] += block.gain;
        }
    }

    // Part 2: Extract top-k' candidates
    topK.Add(score_buf[doc], global_doc_id);
    score_buf[doc] = 0;  // Reset for next window
}
```

**Key optimization**: Process one window at a time for cache locality.

### Step 3: Reranking

```cpp
candidates = topK.TopK();  // Get top-k' candidates

// Exact dot product for final ranking
for (doc in candidates) {
    exact_score = dot_product(query, sparse_vecs[doc]);
    final_topK.Add(exact_score, doc);
}

return final_topK.TopK();  // Final top-k results
```

---

## Algorithm Complexity

### Build Time
- **Time**: O(N × avg_nnz × log(num_bins))
- **Space**: O(vocabulary × num_bins × num_windows)

Where:
- N = number of documents
- avg_nnz = average non-zeros per document
- num_bins = quantization bins (~36-256)
- num_windows = ⌈N / 65536⌉

### Query Time
- **Block selection**: O(query_terms × num_bins)
- **ScatterAdd**: O(selected_blocks × avg_block_size × num_windows)
- **Reranking**: O(k' × avg_nnz)

**Total**: O(query_terms × num_bins + selected_blocks × avg_block_size × num_windows + k' × avg_nnz)

With alpha pruning:
- **Best case**: O(k × avg_nnz) when alpha is very small
- **Worst case**: O(N × avg_nnz) when alpha = 1.0

---

## Key Differences from Bloom Filters

| Aspect | Bloom Filter | QBlock BitQ |
|--------|--------------|-------------|
| **Data Structure** | Bit vector per item | Inverted index with quantized blocks |
| **Granularity** | Per-document or per-granule | Per term-block-window |
| **Query Type** | Membership test | Top-k ranked retrieval |
| **Scoring** | No scores | Yes (quantized) |
| **Pruning** | Binary (in/out) | Alpha-based block pruning |
| **Output** | Boolean | Ranked document IDs |
| **False Positives** | Yes (tunable) | No (approximate ranking) |
| **Purpose** | Filtering | Retrieval |

---

## Why This is NOT Like Bloom Filters

### 1. **Inverted Index Structure**

Bloom filters: `BitVector per entity`
```
Doc 1: [0,1,0,1,1,0,...]  ← bloom filter for doc 1
Doc 2: [1,0,1,0,1,1,...]  ← bloom filter for doc 2
...
```

QBlock: `Inverted posting lists with quantized blocks`
```
Term "machine":
  Block 200-255 (high scores): [doc_1, doc_45, doc_89, ...]
  Block 150-199 (medium scores): [doc_3, doc_12, doc_67, ...]
  Block 100-149 (low scores): [doc_5, doc_8, doc_23, ...]
  ...
```

### 2. **Scoring and Ranking**

Bloom filters: `contains(term) → {true, false}`

QBlock: `score(query, doc) = Σ quant_value[block] × query_weight`

### 3. **Pruning Strategy**

Bloom filters: **Per-granule filtering**
- Check bloom filter for granule
- If no match → skip entire granule
- Binary decision (scan or skip)

QBlock: **Block-max pruning**
- Score all term-block pairs
- Select top blocks by contribution
- Continuous alpha parameter (0.3 to 1.0)

### 4. **Query Execution**

Bloom filter query:
```cpp
for (granule in all_granules) {
    if (!bloom_filter.contains_all_query_terms(granule)) {
        skip(granule);  // Binary decision
    } else {
        scan(granule);
    }
}
```

QBlock query:
```cpp
// 1. Select promising blocks
blocks = [];
for (term in query) {
    for (block in inverted_index[term]) {
        gain = block_max_score × query_weight;
        blocks.push({term, block, gain});
    }
}

// 2. Prune blocks by alpha
selected = top_alpha_blocks(blocks);

// 3. Accumulate scores from selected blocks only
for (block in selected) {
    for (doc in block.documents) {
        scores[doc] += block.gain;
    }
}

// 4. Return top-k by score
return top_k(scores);
```

---

## What QBlock Actually Benchmarks

My benchmark measured:

**Build time**: How fast can we build a quantized block-max inverted index
- 8.8M documents → 6.3 GB index in 9 seconds
- **986K docs/sec**

**Query time**: How fast can we retrieve top-k with block pruning
- Alpha=0.5 → 264 QPS at 90% recall
- **Throughput depends on alpha** (pruning aggressiveness)

**Memory**: Inverted index + forward index size
- 730 bytes/doc (inverted + forward)
- Includes quantized posting lists + original vectors

**Accuracy**: Recall@k with approximate block selection
- Alpha controls recall-latency tradeoff
- 76% (fast) to 98% (slow)

---

## Comparison with Standard Inverted Index

### Standard Inverted Index

```cpp
map<term, list<(doc_id, score)>> posting_lists;

// Query: exhaustive scoring
for (term in query) {
    for ((doc, score) in posting_lists[term]) {
        accumulator[doc] += query_weight × score;
    }
}
return top_k(accumulator);
```

**Complexity**: O(query_terms × avg_posting_length)

### QBlock BitQ

```cpp
map<term, map<block, list<doc_id>>> quantized_index;

// Query: block pruning
blocks = rank_blocks_by_contribution();
selected = top_alpha(blocks);

for (block in selected) {
    for (doc in block) {
        accumulator[doc] += block_max_score;
    }
}
return top_k(accumulator);
```

**Complexity**: O(query_terms × num_blocks + selected_blocks × block_size)

**Speedup**: When alpha < 1.0, we process fewer documents than exhaustive search.

---

## Analogies to Other Systems

### QBlock is similar to:

1. **Lucene Block-Max WAND** (inverted index with block-max scores)
2. **Faiss IVFPQ** (inverted file with product quantization)
3. **ScaNN** (learned quantization with pruning)

### QBlock is NOT like:

1. **Bloom filters** (membership testing, not retrieval)
2. **LSH** (hash-based approximate search)
3. **HNSW** (graph-based vector search)

---

## When to Use QBlock

✅ **Good for**:
- Large-scale sparse vector search (millions of docs)
- Approximate top-k retrieval (90-98% recall acceptable)
- Latency-constrained serving (1-10ms budgets)
- Learned sparse representations (SPLADE, DeepImpact)

❌ **Not good for**:
- Exact search (use exhaustive inverted index)
- Dense vectors (use HNSW, IVF, or ScaNN)
- Membership testing (use bloom filters or hash tables)
- Very small datasets (< 100K docs, exhaustive is fine)

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│ QBlock BitQ Index Structure                              │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  Vocabulary (30K terms)                                  │
│  └── Term 0: "machine"                                   │
│       ├── Block 255 (max score 3.0): [docs with 3.0]    │
│       ├── Block 200 (score 2.35): [docs with 2.35]      │
│       ├── Block 150 (score 1.76): [docs with 1.76]      │
│       └── ...                                             │
│  └── Term 1: "learning"                                  │
│       ├── Block 255: [...]                              │
│       └── ...                                             │
│                                                           │
│  Documents (8.8M docs)                                   │
│  └── Window 0 (docs 0-65535)                            │
│       ├── Document 0: [term_1: 2.5, term_45: 1.2, ...]  │
│       ├── Document 1: [term_3: 0.8, term_67: 2.1, ...]  │
│       └── ...                                             │
│  └── Window 1 (docs 65536-131071)                       │
│       └── ...                                             │
│                                                           │
└─────────────────────────────────────────────────────────┘

Query Execution:
┌────────────┐     ┌──────────────┐     ┌─────────────┐
│  Query:    │     │ Selected     │     │ Top-k'      │
│ "machine   │ ──> │ Blocks       │ ──> │ Candidates  │
│  learning" │     │ (α=0.5)      │     │ (k'=50)     │
└────────────┘     └──────────────┘     └─────────────┘
                           │                     │
                           v                     v
                   ┌──────────────┐     ┌─────────────┐
                   │ ScatterAdd   │     │ Rerank with │
                   │ Accumulate   │     │ Exact Scores│
                   │ Scores       │     │             │
                   └──────────────┘     └─────────────┘
                                               │
                                               v
                                        ┌─────────────┐
                                        │ Final Top-k │
                                        │ Results     │
                                        └─────────────┘
```

---

## Conclusion

**QBlock BitQ is NOT a bloom filter-based system.** It's a:

1. **Block-Max Quantized Inverted Index**
2. **With block-level pruning** (similar to WAND/BMW)
3. **For approximate top-k sparse vector retrieval**
4. **With tunable recall-latency tradeoff** (alpha parameter)

The benchmark I ran measures **inverted index performance**, not bloom filter performance.

**My previous comparison was incorrect** because:
- I compared bloom filters (membership testing) with QBlock (top-k retrieval)
- These solve different problems
- They're not directly comparable

**The correct comparison would be**:
- **QBlock vs Lucene** (both are inverted indexes with block-max pruning)
- **Bloom filters vs Hash tables** (both are membership testing structures)

---

*Analysis Date*: 2024-01-26
*QBlock Version*: 0.0.4
*Source*: `/home/ubuntu/bitq-code/cpp-sparse-ann/cpp/src/BitQIndex.cpp`
