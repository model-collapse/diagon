# Unified SIMD-Accelerated Storage Architecture
## Merging Inverted Index and Column Storage with SIMD Operations

Source references:
- SINDI Paper: https://arxiv.org/abs/2509.08395 (rank_features approach)
- ClickHouse Column Storage: `03_COLUMN_STORAGE.md`
- Lucene BM25Similarity: `org.apache.lucene.search.similarities.BM25Similarity`
- Lucene rank_features: `org.apache.lucene.document.FeatureField`

## Problem Statement

Current design has three separate storage systems:

1. **Inverted Index** (PostingsFormat): Stores (docID, freq, positions)
2. **Column Storage** (DocValuesFormat, ColumnFormat): Stores dense columns
3. **SIMD Postings** (SindiPostingsFormat): Stores (docID, precomputed_value)

**Issues:**
- **Storage duplication**: Same data in multiple formats
- **Limited scope**: SINDI only works for static weights (rank_features)
- **Missing integration**: Columns and posting lists not unified
- **BM25 not addressed**: Dynamic scoring (tf, doc length) requires runtime computation

## Key Insight: Posting Lists ≈ Sparse Columns

**Traditional view:**
```
Inverted Index: term → [docID₁, docID₂, ...] (sparse)
Columns:        field → [value₀, value₁, value₂, ...] (dense)
               ↑                    ↑
          Different storage    Different access
```

**Unified view:**
```
Both are columns with different sparsity:

Posting list for "wireless": Column<int, float>
  docID: [5,    12,   23,   45,   ...]   ← sparse indices
  tf:    [2,    1,    3,    1,    ...]   ← values

DocValues for "price": Column<int, float>
  docID: [0, 1, 2, 3, 4, ...]           ← dense indices (implicit)
  price: [99, 0, 129, 0, 79, ...]       ← values (0 = missing)

Both support SIMD operations!
```

## Architecture Overview

### Three-Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│           QUERY LAYER (Module 07)                           │
│  - BM25 Scorer (dynamic)                                    │
│  - rank_features Scorer (static weights)                    │
│  - Filter Processor (SIMD)                                  │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│         COMPUTATION LAYER (NEW)                             │
│  - SIMD Scatter-Add (OR/AND queries)                        │
│  - SIMD BM25 Formula (vectorized scoring)                   │
│  - SIMD Filter Evaluation (range, equality)                 │
└──────────────────┬──────────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────────┐
│       UNIFIED STORAGE LAYER (Module 02 + 03)                │
│  ┌──────────────────┐  ┌────────────────────┐              │
│  │ Sparse Columns   │  │  Dense Columns     │              │
│  │ (Posting Lists)  │  │  (DocValues)       │              │
│  │ - tf values      │  │  - doc_length      │              │
│  │ - positions      │  │  - price           │              │
│  │ - payloads       │  │  - rating          │              │
│  └──────────────────┘  └────────────────────┘              │
│                                                              │
│  Shared: Window-based partitioning, SIMD alignment          │
└──────────────────────────────────────────────────────────────┘
```

## Storage Layer Design

### Unified Window-Based Column Storage

```cpp
/**
 * Unified column interface for both sparse and dense data
 *
 * Supports:
 * - Sparse columns (posting lists): ~1-10% density
 * - Dense columns (doc values): 50-100% density
 * - SIMD operations on both
 */
enum class ColumnDensity {
    SPARSE,     // < 10% non-zero (use posting list format)
    MEDIUM,     // 10-50% non-zero (use bitmap + values)
    DENSE       // > 50% non-zero (use full array)
};

/**
 * Window: Fixed-size partition of column data
 * Shared by both inverted index and column storage
 */
template<typename ValueType>
struct ColumnWindow {
    int docIdBase;              // Base doc ID for window
    int capacity;               // Window size (e.g., 100K)
    ColumnDensity density;

    // Sparse representation (for posting lists)
    std::vector<int> indices;           // Doc IDs (sorted)
    std::vector<ValueType> values;      // Values at those doc IDs

    // Dense representation (for doc values)
    std::vector<ValueType> denseValues; // Full array [0...capacity)
    std::unique_ptr<BitSet> nullBitmap; // Which docs have values

    /**
     * Get value for doc ID (unified interface)
     */
    std::optional<ValueType> get(int docId) const {
        int localDoc = docId - docIdBase;

        if (density == ColumnDensity::SPARSE) {
            // Binary search in sparse indices
            auto it = std::lower_bound(indices.begin(), indices.end(), docId);
            if (it != indices.end() && *it == docId) {
                size_t idx = std::distance(indices.begin(), it);
                return values[idx];
            }
            return std::nullopt;
        } else {
            // Direct array access
            if (nullBitmap && !nullBitmap->get(localDoc)) {
                return std::nullopt;
            }
            return denseValues[localDoc];
        }
    }

    /**
     * SIMD batch get (for multiple doc IDs)
     * Returns values aligned for SIMD operations
     */
    void batchGet(const std::vector<int>& docIds,
                  std::vector<ValueType>& output) const {
        output.resize(docIds.size());

        if (density == ColumnDensity::SPARSE) {
            // Merge join sparse indices with requested docIds
            size_t i = 0, j = 0;
            while (i < docIds.size() && j < indices.size()) {
                if (docIds[i] == indices[j]) {
                    output[i] = values[j];
                    i++; j++;
                } else if (docIds[i] < indices[j]) {
                    output[i] = ValueType{};  // Zero
                    i++;
                } else {
                    j++;
                }
            }
            // Fill remaining with zeros
            while (i < docIds.size()) {
                output[i++] = ValueType{};
            }
        } else {
            // Direct array access (SIMD-friendly)
            for (size_t i = 0; i < docIds.size(); ++i) {
                int localDoc = docIds[i] - docIdBase;
                output[i] = denseValues[localDoc];
            }
        }
    }

    /**
     * SIMD scatter-add operation
     * Accumulates values into shared array
     */
    void simdScatterAdd(
        float multiplier,
        std::vector<float>& accumulator) const {

        if (density == ColumnDensity::SPARSE) {
            // SIMD multiply values
            std::vector<float> products(values.size());
            __m256 mult = _mm256_set1_ps(multiplier);

            size_t i = 0;
            for (; i + 8 <= values.size(); i += 8) {
                __m256 vals = _mm256_loadu_ps(&values[i]);
                __m256 prods = _mm256_mul_ps(mult, vals);
                _mm256_storeu_ps(&products[i], prods);
            }

            // Handle remainder
            for (; i < values.size(); ++i) {
                products[i] = multiplier * values[i];
            }

            // Scatter-add to accumulator
            for (size_t i = 0; i < indices.size(); ++i) {
                int localDoc = indices[i] - docIdBase;
                accumulator[localDoc] += products[i];
            }

        } else {
            // SIMD multiply entire dense array
            __m256 mult = _mm256_set1_ps(multiplier);

            size_t i = 0;
            for (; i + 8 <= capacity; i += 8) {
                __m256 vals = _mm256_loadu_ps(&denseValues[i]);
                __m256 acc = _mm256_loadu_ps(&accumulator[i]);
                __m256 prods = _mm256_mul_ps(mult, vals);
                __m256 result = _mm256_add_ps(acc, prods);
                _mm256_storeu_ps(&accumulator[i], result);
            }

            // Handle remainder
            for (; i < capacity; ++i) {
                accumulator[i] += multiplier * denseValues[i];
            }
        }
    }
};
```

### Unified Column Format

```cpp
/**
 * Unified format supporting both posting lists and doc values
 *
 * Replaces:
 * - PostingsFormat (sparse columns)
 * - DocValuesFormat (dense columns)
 * - ColumnFormat (ClickHouse columns)
 */
class UnifiedColumnFormat : public CodecFormat {
public:
    struct ColumnMetadata {
        std::string name;
        ColumnDensity density;
        DataType valueType;          // int32, float32, binary, etc.

        // For sparse columns (posting lists)
        bool hasFrequencies{false};  // Store term frequencies?
        bool hasPositions{false};    // Store positions?
        bool hasPayloads{false};     // Store payloads?

        // For dense columns (doc values)
        bool hasNulls{false};        // Nullable column?

        // Statistics for query optimization
        int64_t totalDocs;
        int64_t nonZeroDocs;
        float avgValue;
        float maxValue;
    };

    // ==================== Write API ====================

    /**
     * Write sparse column (posting list)
     */
    void writeSparseColumn(
        const std::string& columnName,
        const std::vector<Posting>& postings,
        ColumnMetadata metadata) {

        // Group postings into windows
        auto windows = partitionIntoWindows(postings, windowSize_);

        for (auto& window : windows) {
            // Write window header
            output_->writeVInt(window.docIdBase);
            output_->writeVInt(window.indices.size());

            // Write indices (delta-encoded)
            int prevDoc = window.docIdBase;
            for (int doc : window.indices) {
                output_->writeVInt(doc - prevDoc);
                prevDoc = doc;
            }

            // Write values (type-specific)
            writeValues(window.values, metadata.valueType);

            // Write positions if needed
            if (metadata.hasPositions) {
                writePositions(window);
            }
        }
    }

    /**
     * Write dense column (doc values)
     */
    void writeDenseColumn(
        const std::string& columnName,
        const std::vector<float>& values,
        ColumnMetadata metadata) {

        // Partition into windows
        for (size_t base = 0; base < values.size(); base += windowSize_) {
            size_t count = std::min(windowSize_, values.size() - base);

            // Write window
            output_->writeVInt(base);
            output_->writeVInt(count);

            // Write null bitmap if needed
            if (metadata.hasNulls) {
                writeNullBitmap(values, base, count);
            }

            // Write values (SIMD-aligned)
            writeValuesSIMD(&values[base], count);
        }
    }

private:
    size_t windowSize_{100000};  // Shared window size
    IndexOutput* output_;
};
```

## BM25 with SIMD Acceleration

### Problem: BM25 Requires Dynamic Computation

**BM25 Formula:**
```
score(q,d) = Σ_{t∈q} IDF(t) × (tf(t,d) × (k₁+1)) / (tf(t,d) + k₁ × (1-b+b×|d|/avgdl))
                ↑                  ↑                                    ↑
           Static IDF      Dynamic (per doc)                    Dynamic (per doc)
```

**What to store:**
- ✅ IDF(t): Static, precompute and store in term dictionary
- ✅ tf(t,d): Store in posting list (sparse column)
- ✅ |d|: Store in dense column (doc_length field)
- ❌ Cannot fully precompute: BM25 score depends on query terms

**Solution: SIMD-accelerate the BM25 formula**

```cpp
/**
 * SIMD-accelerated BM25 scorer
 *
 * Computes BM25 formula using vectorized operations
 */
class SIMDBm25Scorer {
public:
    SIMDBm25Scorer(
        float k1 = 1.2f,
        float b = 0.75f,
        float avgDocLength = 100.0f)
        : k1_(k1)
        , b_(b)
        , avgDocLength_(avgDocLength) {}

    /**
     * Score OR query with SIMD BM25
     *
     * @param queryTerms Terms with precomputed IDF weights
     * @param tfColumns Sparse columns for term frequencies
     * @param docLengthColumn Dense column for document lengths
     */
    std::vector<std::pair<int, float>> scoreOrQuery(
        const std::vector<std::pair<std::string, float>>& queryTerms,  // (term, IDF)
        const std::map<std::string, ColumnWindow<int>*>& tfColumns,
        const ColumnWindow<int>& docLengthColumn,
        int topK) {

        size_t windowSize = docLengthColumn.capacity;
        std::vector<float> accumulator(windowSize, 0.0f);

        // Precompute BM25 constants (SIMD-friendly)
        __m256 k1_vec = _mm256_set1_ps(k1_);
        __m256 b_vec = _mm256_set1_ps(b_);
        __m256 avgdl_vec = _mm256_set1_ps(avgDocLength_);
        __m256 one_minus_b = _mm256_set1_ps(1.0f - b_);
        __m256 k1_plus_1 = _mm256_set1_ps(k1_ + 1.0f);

        // Process each query term
        for (const auto& [term, idf] : queryTerms) {
            auto* tfColumn = tfColumns.at(term);

            // Get tf values for docs containing this term
            const auto& docIds = tfColumn->indices;
            const auto& tfValues = tfColumn->values;

            // Batch get document lengths for these docs
            std::vector<int> docLengths(docIds.size());
            docLengthColumn.batchGet(docIds, docLengths);

            // SIMD BM25 computation
            std::vector<float> termScores(docIds.size());

            for (size_t i = 0; i + 8 <= docIds.size(); i += 8) {
                // Load tf and doc lengths
                __m256 tf = _mm256_cvtepi32_ps(_mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(&tfValues[i])));
                __m256 doclen = _mm256_cvtepi32_ps(_mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(&docLengths[i])));

                // Compute: (1 - b + b * doclen / avgdl)
                __m256 norm = _mm256_div_ps(doclen, avgdl_vec);
                norm = _mm256_mul_ps(b_vec, norm);
                norm = _mm256_add_ps(one_minus_b, norm);

                // Compute: tf + k1 * norm
                __m256 denom = _mm256_mul_ps(k1_vec, norm);
                denom = _mm256_add_ps(tf, denom);

                // Compute: tf * (k1 + 1) / denom
                __m256 numer = _mm256_mul_ps(tf, k1_plus_1);
                __m256 score = _mm256_div_ps(numer, denom);

                // Multiply by IDF
                __m256 idf_vec = _mm256_set1_ps(idf);
                score = _mm256_mul_ps(score, idf_vec);

                // Store results
                _mm256_storeu_ps(&termScores[i], score);
            }

            // Handle remainder (scalar)
            for (size_t i = (docIds.size() / 8) * 8; i < docIds.size(); ++i) {
                float tf = tfValues[i];
                float doclen = docLengths[i];
                float norm = 1.0f - b_ + b_ * doclen / avgDocLength_;
                float score = idf * (tf * (k1_ + 1.0f)) / (tf + k1_ * norm);
                termScores[i] = score;
            }

            // Scatter-add to accumulator
            for (size_t i = 0; i < docIds.size(); ++i) {
                int localDoc = docIds[i] - docLengthColumn.docIdBase;
                accumulator[localDoc] += termScores[i];
            }
        }

        // Extract top K
        return extractTopK(accumulator, topK, docLengthColumn.docIdBase);
    }

private:
    float k1_;
    float b_;
    float avgDocLength_;

    std::vector<std::pair<int, float>> extractTopK(
        const std::vector<float>& scores,
        int topK,
        int docIdBase) {

        std::vector<std::pair<int, float>> results;
        results.reserve(topK);

        // Use partial sort
        std::vector<std::pair<float, int>> candidates;
        for (size_t i = 0; i < scores.size(); ++i) {
            if (scores[i] > 0) {
                candidates.push_back({scores[i], docIdBase + i});
            }
        }

        std::partial_sort(candidates.begin(),
            candidates.begin() + std::min(topK, (int)candidates.size()),
            candidates.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        for (size_t i = 0; i < std::min(topK, (int)candidates.size()); ++i) {
            results.push_back({candidates[i].second, candidates[i].first});
        }

        return results;
    }
};
```

## rank_features Mode (Static Weights)

```cpp
/**
 * rank_features scorer: Static precomputed weights
 *
 * Use case: SPLADE, learned embeddings, document embeddings
 * No dynamic computation needed
 */
class RankFeaturesScorer {
public:
    /**
     * Score OR query with static weights (SINDI-style)
     *
     * @param queryTerms Terms with query-time weights
     * @param featureColumns Sparse columns with precomputed static weights
     */
    std::vector<std::pair<int, float>> scoreOrQuery(
        const std::vector<std::pair<std::string, float>>& queryTerms,
        const std::map<std::string, ColumnWindow<float>*>& featureColumns,
        int topK) {

        // Much simpler: just multiply and scatter-add
        size_t windowSize = featureColumns.begin()->second->capacity;
        std::vector<float> accumulator(windowSize, 0.0f);

        for (const auto& [term, queryWeight] : queryTerms) {
            auto* column = featureColumns.at(term);

            // SIMD scatter-add (values already precomputed)
            column->simdScatterAdd(queryWeight, accumulator);
        }

        return extractTopK(accumulator, topK, /*docIdBase=*/0);
    }
};
```

## Unified Query Processor

```cpp
/**
 * Unified SIMD query processor
 *
 * Supports:
 * - BM25 scoring (dynamic computation)
 * - rank_features scoring (static weights)
 * - Filters (SIMD range checks)
 */
class UnifiedSIMDQueryProcessor {
public:
    enum class ScoringMode {
        BM25,           // Dynamic BM25 computation
        RANK_FEATURES,  // Static precomputed weights
        TF_IDF          // Classic TF-IDF
    };

    explicit UnifiedSIMDQueryProcessor(
        const UnifiedColumnReader& reader,
        ScoringMode mode = ScoringMode::BM25)
        : reader_(reader)
        , mode_(mode)
        , bm25Scorer_(1.2f, 0.75f, 100.0f)
        , rankFeaturesScorer_() {}

    /**
     * Execute OR query with appropriate scoring
     */
    TopDocs searchOr(
        const std::vector<std::pair<std::string, float>>& queryTerms,
        FilterPtr filter,
        int topK) {

        std::vector<std::pair<int, float>> results;

        if (mode_ == ScoringMode::BM25) {
            // Load tf columns (sparse)
            std::map<std::string, ColumnWindow<int>*> tfColumns;
            for (const auto& [term, weight] : queryTerms) {
                tfColumns[term] = reader_.getSparseColumn<int>(term);
            }

            // Load doc_length column (dense)
            auto* docLengthColumn = reader_.getDenseColumn<int>("doc_length");

            // SIMD BM25 scoring
            results = bm25Scorer_.scoreOrQuery(
                queryTerms, tfColumns, *docLengthColumn, topK);

        } else if (mode_ == ScoringMode::RANK_FEATURES) {
            // Load feature columns (sparse, precomputed)
            std::map<std::string, ColumnWindow<float>*> featureColumns;
            for (const auto& [term, weight] : queryTerms) {
                featureColumns[term] = reader_.getSparseColumn<float>(term);
            }

            // SIMD scatter-add (no BM25 formula)
            results = rankFeaturesScorer_.scoreOrQuery(
                featureColumns, queryTerms, topK);
        }

        // Apply filters (also SIMD-accelerated)
        if (filter) {
            results = applyFilterSIMD(results, filter);
        }

        // Convert to TopDocs
        return toTopDocs(results);
    }

private:
    const UnifiedColumnReader& reader_;
    ScoringMode mode_;

    SIMDBm25Scorer bm25Scorer_;
    RankFeaturesScorer rankFeaturesScorer_;

    std::vector<std::pair<int, float>> applyFilterSIMD(
        const std::vector<std::pair<int, float>>& candidates,
        FilterPtr filter) {

        // Extract doc IDs
        std::vector<int> docIds;
        docIds.reserve(candidates.size());
        for (const auto& [doc, score] : candidates) {
            docIds.push_back(doc);
        }

        // Get filter column (e.g., price)
        // This can also use SIMD for batch evaluation
        auto* filterColumn = reader_.getDenseColumn<float>(filter->getFieldName());

        std::vector<float> filterValues;
        filterColumn->batchGet(docIds, filterValues);

        // SIMD range check
        std::vector<bool> passesFilter = simdRangeCheck(
            filterValues, filter->getMin(), filter->getMax());

        // Filter results
        std::vector<std::pair<int, float>> filtered;
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (passesFilter[i]) {
                filtered.push_back(candidates[i]);
            }
        }

        return filtered;
    }

    std::vector<bool> simdRangeCheck(
        const std::vector<float>& values,
        float min, float max) {

        std::vector<bool> results(values.size());
        __m256 min_vec = _mm256_set1_ps(min);
        __m256 max_vec = _mm256_set1_ps(max);

        size_t i = 0;
        for (; i + 8 <= values.size(); i += 8) {
            __m256 vals = _mm256_loadu_ps(&values[i]);

            // Check: val >= min AND val <= max
            __m256 ge_min = _mm256_cmp_ps(vals, min_vec, _CMP_GE_OQ);
            __m256 le_max = _mm256_cmp_ps(vals, max_vec, _CMP_LE_OQ);
            __m256 mask = _mm256_and_ps(ge_min, le_max);

            // Extract results
            int maskBits = _mm256_movemask_ps(mask);
            for (int j = 0; j < 8; ++j) {
                results[i + j] = (maskBits >> j) & 1;
            }
        }

        // Handle remainder
        for (; i < values.size(); ++i) {
            results[i] = values[i] >= min && values[i] <= max;
        }

        return results;
    }
};
```

---

## Adaptive Filter Strategy Selection

When combining filters with SIMD scatter-add scoring, there are two main strategies with different performance characteristics:

### Strategy 1: List Merge Scanning (Traditional)
```cpp
// Apply filter first → extract matching docs → SIMD scatter-add only on filtered docs
DocIdSet filteredDocs = filter->getDocIdSet(context);
std::vector<int> matchingDocs = filteredDocs.extractDocIds();

// SIMD score only matching docs (requires gather operations)
for (const auto& term : queryTerms) {
    tfColumn->simdScatterAddGather(term.weight, matchingDocs, scores);
}
```

**Characteristics:**
- ✅ Process only filtered docs (skip work for excluded docs)
- ❌ Requires gathering TF values at random positions (cache misses)
- ❌ BitSet extraction has branching overhead
- **Best when**: Very low selectivity (<1-2% docs pass filter)

### Strategy 2: Pre-Fill Score Buffer (Branchless SIMD)
```cpp
// Initialize all scores to -∞
std::vector<float> scores(maxDoc, -INFINITY);

// Set scores to 0 for docs passing filter
DocIdSet filteredDocs = filter->getDocIdSet(context);
for (int doc : filteredDocs) {
    scores[doc] = 0.0f;  // Or use vectorized mask operation
}

// SIMD scatter-add on ALL docs (sequential access, -∞ + x = -∞)
for (const auto& term : queryTerms) {
    tfColumn->simdScatterAddSequential(term.weight, scores);
}

// Filter out -∞ scores
TopDocs results = heapSelectFinite(scores, topK);
```

**Characteristics:**
- ✅ Full SIMD utilization with sequential memory access
- ✅ No gather operations (hardware prefetcher friendly)
- ✅ Branchless execution (-∞ arithmetic is transparent)
- ❌ Processes all docs including filtered-out ones
- **Best when**: Low-to-medium selectivity (>1-2% docs pass filter)

### Cost Model and Decision Logic

**Key insight**: The crossover point depends on **gather cost vs sequential processing cost**.

Gather operations (random access):
- ~10 cycles per doc (cache miss penalty)
- Destroys hardware prefetcher effectiveness

Sequential SIMD operations:
- ~1/8 cycles per doc (AVX2 with 8-wide vectors)
- Highly cache-efficient

**Cost estimates** (N=100K docs, T=3 terms, W=8 SIMD width):

| Selectivity | Matching Docs | List Merge Cost | Pre-Fill Cost | Winner |
|-------------|---------------|-----------------|---------------|---------|
| 1%          | 1K            | 37K cycles      | 64K cycles    | List Merge |
| 5%          | 5K            | 155K cycles     | 65K cycles    | Pre-Fill (2.4×) |
| 10%         | 10K           | 355K cycles     | 73K cycles    | Pre-Fill (4.9×) |
| 50%         | 50K           | 1,808K cycles   | 113K cycles   | Pre-Fill (16×) |
| 90%         | 90K           | 3,185K cycles   | 76K cycles    | Pre-Fill (42×) |

**Conclusion**: Pre-fill wins for almost all realistic scenarios except very low selectivity (<1-2%).

### Implementation: Dynamic Strategy Selection

```cpp
enum class FilterStrategy {
    LIST_MERGE,      // Extract docs → gather → scatter-add
    PREFILL_SPARSE,  // Pre-fill -∞ → sparse set to 0 → sequential scatter
    PREFILL_DENSE    // Pre-fill -∞ → masked write → sequential scatter
};

class AdaptiveFilteredScorer {
public:
    TopDocs score(
        const std::vector<std::pair<std::string, float>>& queryTerms,
        FilterPtr filter,
        int topK) {

        // Estimate filter selectivity
        float selectivity = estimateSelectivity(filter);

        // Choose strategy based on cost model
        FilterStrategy strategy = selectStrategy(
            numDocs_, selectivity, queryTerms.size());

        switch (strategy) {
            case FilterStrategy::LIST_MERGE:
                return scoreListMerge(queryTerms, filter, topK);
            case FilterStrategy::PREFILL_SPARSE:
                return scorePrefillSparse(queryTerms, filter, topK);
            case FilterStrategy::PREFILL_DENSE:
                return scorePrefillDense(queryTerms, filter, topK);
        }
    }

private:
    FilterStrategy selectStrategy(
        int numDocs,
        float selectivity,
        int numTerms) {

        // Very low selectivity: list merge might win
        if (selectivity < 0.01f) {
            int matchingDocs = static_cast<int>(numDocs * selectivity);

            // Estimate costs
            double listMergeCost =
                matchingDocs * 10.0 * numTerms;  // Gather-dominated

            double prefillCost =
                numDocs / 8.0 * numTerms +       // Sequential SIMD
                matchingDocs;                     // Sparse initialization

            if (listMergeCost < prefillCost) {
                return FilterStrategy::LIST_MERGE;
            }
        }

        // Choose between sparse and dense pre-fill
        // Dense is better for high selectivity (>50%)
        return (selectivity > 0.5f)
            ? FilterStrategy::PREFILL_DENSE
            : FilterStrategy::PREFILL_SPARSE;
    }

    float estimateSelectivity(FilterPtr filter) {
        // Option 1: Use skip index statistics (MinMax)
        if (auto* rangeFilter = dynamic_cast<RangeFilter*>(filter.get())) {
            auto skipIndex = reader_.getSkipIndex(rangeFilter->getFieldName());
            int passingGranules = skipIndex->countPassingGranules(rangeFilter);
            return static_cast<float>(passingGranules) / totalGranules_;
        }

        // Option 2: Use cached filter statistics
        if (filterStatsCache_.contains(filter->getCacheKey())) {
            return filterStatsCache_[filter->getCacheKey()].selectivity;
        }

        // Option 3: Conservative default
        return 0.5f;
    }

    TopDocs scoreListMerge(
        const std::vector<std::pair<std::string, float>>& queryTerms,
        FilterPtr filter,
        int topK) {

        // Step 1: Apply filter
        DocIdSet filteredDocs = filter->getDocIdSet(context_);
        std::vector<int> matchingDocs = filteredDocs.extractDocIds();

        // Step 2: Initialize scores for matching docs only
        std::vector<float> scores(matchingDocs.size(), 0.0f);

        // Step 3: SIMD scatter-add with gather
        for (const auto& [term, weight] : queryTerms) {
            auto* tfColumn = reader_.getSparseColumn<int>(term);
            tfColumn->batchGetAndAccumulate(matchingDocs, weight, scores);
        }

        // Step 4: Build TopDocs
        return heapSelect(matchingDocs, scores, topK);
    }

    TopDocs scorePrefillSparse(
        const std::vector<std::pair<std::string, float>>& queryTerms,
        FilterPtr filter,
        int topK) {

        // Step 1: Initialize all to -∞
        std::vector<float> scores(numDocs_, -INFINITY);

        // Step 2: Set passing docs to 0 (sparse iteration)
        DocIdSet filteredDocs = filter->getDocIdSet(context_);
        for (int doc : filteredDocs) {
            scores[doc] = 0.0f;
        }

        // Step 3: SIMD scatter-add on all docs (sequential)
        for (const auto& [term, weight] : queryTerms) {
            auto* tfColumn = reader_.getSparseColumn<int>(term);
            tfColumn->simdScatterAddSequential(weight, scores);
        }

        // Step 4: Filter finite scores and select top-K
        return heapSelectFinite(scores, topK);
    }

    TopDocs scorePrefillDense(
        const std::vector<std::pair<std::string, float>>& queryTerms,
        FilterPtr filter,
        int topK) {

        // Step 1: Initialize all to -∞ (vectorized)
        std::vector<float> scores(numDocs_);
        __m256 neg_inf = _mm256_set1_ps(-INFINITY);
        for (size_t i = 0; i < numDocs_; i += 8) {
            _mm256_storeu_ps(&scores[i], neg_inf);
        }

        // Step 2: Masked write for passing docs (vectorized)
        DocIdSet filteredDocs = filter->getDocIdSet(context_);
        if (auto* bitSet = filteredDocs.getBitSet()) {
            __m256 zero = _mm256_setzero_ps();
            for (size_t i = 0; i < numDocs_; i += 8) {
                // Load 8 bits from BitSet
                uint8_t mask = bitSet->getByte(i / 8);
                __m256i vmask = _mm256_cvtepu8_epi32(_mm_set1_epi8(mask));
                _mm256_maskstore_ps(&scores[i], vmask, zero);
            }
        }

        // Step 3: SIMD scatter-add (same as sparse)
        for (const auto& [term, weight] : queryTerms) {
            auto* tfColumn = reader_.getSparseColumn<int>(term);
            tfColumn->simdScatterAddSequential(weight, scores);
        }

        // Step 4: Filter finite scores and select top-K
        return heapSelectFinite(scores, topK);
    }

    const UnifiedColumnReader& reader_;
    int numDocs_;
    int totalGranules_;
    std::unordered_map<std::string, FilterStats> filterStatsCache_;
};
```

### Hybrid Approach: Granule-Level Adaptation

For maximum efficiency, apply strategy selection at the granule level:

```cpp
TopDocs scoreHybrid(
    const std::vector<std::pair<std::string, float>>& queryTerms,
    FilterPtr filter,
    int topK) {

    // Step 1: Use skip indexes to prune granules (coarse filtering)
    std::vector<int> candidateGranules = skipIndex_->filterGranules(filter);

    std::vector<std::pair<int, float>> allResults;

    // Step 2: Process each granule with adaptive strategy
    for (int granuleId : candidateGranules) {
        // Estimate selectivity within this granule
        float granuleSelectivity = skipIndex_->estimateGranuleSelectivity(
            granuleId, filter);

        // Choose strategy per granule
        TopDocs granuleResults;
        if (granuleSelectivity < 0.01f) {
            granuleResults = scoreGranuleListMerge(
                granuleId, queryTerms, filter);
        } else {
            granuleResults = scoreGranulePrefill(
                granuleId, queryTerms, filter);
        }

        allResults.insert(allResults.end(),
                         granuleResults.begin(),
                         granuleResults.end());
    }

    // Step 3: Global top-K selection
    return heapSelect(allResults, topK);
}
```

### Experimental Validation (PoC Phase)

The decision boundary (~1-2% selectivity) needs empirical validation:

**Benchmarks:**
1. Vary selectivity: 0.1%, 1%, 5%, 10%, 25%, 50%, 75%, 90%
2. Vary number of terms: 1, 2, 3, 5, 10, 20
3. Vary window size: 10K, 50K, 100K, 500K, 1M
4. Different filter types: range, term, bloom, composite

**Metrics:**
- Cycles per query
- Cache misses (L1, L2, L3)
- Branch mispredictions
- SIMD instruction mix

**See**: `RESEARCH_SIMD_FILTER_STRATEGIES.md` for detailed cost model analysis.

---

## Integration with Existing Modules

### Unified Codec

```cpp
/**
 * Codec with unified storage layer
 *
 * Extends Module 02 (Codec Architecture)
 */
class UnifiedSIMDCodec : public Codec {
public:
    explicit UnifiedSIMDCodec(
        size_t windowSize = 100000,
        bool enableSIMD = true)
        : Codec("UnifiedSIMD")
        , windowSize_(windowSize)
        , enableSIMD_(enableSIMD) {}

    // ==================== Format Accessors ====================

    /**
     * Unified format replaces both PostingsFormat and DocValuesFormat
     */
    PostingsFormat* postingsFormat() override {
        return new UnifiedPostingsFormatAdapter(columnFormat_);
    }

    DocValuesFormat* docValuesFormat() override {
        return new UnifiedDocValuesFormatAdapter(columnFormat_);
    }

    /**
     * Direct access to unified column format
     */
    UnifiedColumnFormat* columnFormat() {
        return columnFormat_.get();
    }

private:
    std::unique_ptr<UnifiedColumnFormat> columnFormat_;
    size_t windowSize_;
    bool enableSIMD_;
};
```

### E-Commerce Query Example

```cpp
// ==================== Setup ====================

// Create unified codec
auto codec = std::make_unique<UnifiedSIMDCodec>(
    100000,  // Window size
    true     // Enable SIMD
);

IndexWriterConfig config;
config.setCodec(std::move(codec));

IndexWriter writer(*directory, config);

// Index documents
Document doc;
doc.add(TextField("description", "wireless bluetooth headphones"));
doc.add(NumericDocValuesField("price", 199.99f));
doc.add(NumericDocValuesField("rating", 4.5f));
doc.add(StoredField("asin", "B08XYZ123"));
writer.addDocument(doc);
writer.commit();

// ==================== Query ====================

auto reader = IndexReader::open(*directory);

// Create unified SIMD processor (BM25 mode)
UnifiedSIMDQueryProcessor processor(
    *reader,
    UnifiedSIMDQueryProcessor::ScoringMode::BM25
);

// Text query
std::vector<std::pair<std::string, float>> queryTerms = {
    {"wireless", 0.0f},    // IDF computed automatically
    {"headphones", 0.0f},
    {"bluetooth", 0.0f}
};

// Filters
auto priceFilter = std::make_shared<RangeFilter>("price", 0, 200);
auto ratingFilter = std::make_shared<RangeFilter>("rating", 4, 5);
auto combinedFilter = std::make_shared<AndFilter>(
    std::vector<FilterPtr>{priceFilter, ratingFilter});

// Execute (BM25 + filters, all SIMD-accelerated)
TopDocs results = processor.searchOr(queryTerms, combinedFilter, 100);

// Calculate average price (column access)
auto* priceColumn = reader->getDenseColumn<float>("price");
double sum = 0.0;
for (const auto& scoreDoc : results.scoreDocs) {
    auto price = priceColumn->get(scoreDoc.doc);
    if (price) {
        sum += *price;
    }
}
double avgPrice = sum / results.scoreDocs.size();

std::cout << "Top 100 average price: $" << avgPrice << "\n";
```

## Performance Analysis

### Complexity Comparison

| Operation | Traditional | Separate SIMD | Unified SIMD |
|-----------|-------------|---------------|--------------|
| BM25 OR query | O(\|q\| × \|d\|) | ❌ Not supported | O(\|q\| / s) |
| rank_features OR | O(\|q\| × \|d\|) | O(\|q\| / s) | O(\|q\| / s) |
| Filter | O(\|f\|) scalar | O(\|f\|) scalar | O(\|f\| / s) |
| Column access | O(1) random | O(1) random | O(n / s) batch |
| Storage | 3 copies | 3 copies | **1 unified** |

Where:
- \|q\| = posting list length
- \|d\| = matching docs
- \|f\| = filtered docs
- s = SIMD width (8 for AVX2)
- n = batch size

### Memory Layout Comparison

**Traditional (3 separate systems):**
```
Inverted Index:  100 GB (compressed posting lists)
DocValues:        50 GB (compressed columns)
SIMD Postings:   120 GB (value-storing lists)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Total:           270 GB
```

**Unified (single storage layer):**
```
Sparse Columns (posting lists):  120 GB (window-partitioned)
Dense Columns (doc values):       50 GB (window-partitioned)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Total:                           170 GB (37% reduction)
```

### Performance Expectations

| Query Type | Traditional | Separate SIMD | Unified SIMD |
|------------|-------------|---------------|--------------|
| BM25 OR (3 terms) | 50ms | ❌ N/A | **12ms** (4.2×) |
| rank_features OR | 50ms | 8ms (6×) | **8ms** (6×) |
| Filter (range) | 10ms | 10ms | **3ms** (3.3×) |
| Column access (100) | 1ms | 1ms | **0.3ms** (3×) |
| **E-commerce query** | **61ms** | ❌ Mixed | **23ms** (2.7×) |

**E-commerce query breakdown (unified SIMD):**
- Text search (BM25): 12ms ← SIMD BM25
- Price filter: 2ms ← SIMD range check
- Rating filter: 2ms ← SIMD range check
- Column access (top 100): 0.3ms ← Batch SIMD
- Average computation: 0.1ms ← SIMD reduction
- **Total: 16.4ms** (vs. 61ms traditional = **3.7× speedup**)

## Implementation Priority

### Phase 1: Storage Layer
1. ✅ Window-based column storage (sparse + dense)
2. ✅ Unified file format
3. ✅ Adapter for existing PostingsFormat/DocValuesFormat APIs

### Phase 2: SIMD Operations
1. ✅ SIMD scatter-add (OR queries)
2. ✅ SIMD BM25 formula
3. ✅ SIMD range checks (filters)

### Phase 3: Query Integration
1. ✅ UnifiedSIMDQueryProcessor
2. ✅ IndexSearcher integration
3. ✅ Backward compatibility

---

**Design Status**: Complete ✅
**Integration**: Unifies modules 02 (Codec), 03 (Columns), 07 (Query), 07a (Filters), 13 (SIMD)
**Key Innovation**: Single storage layer for both inverted index and columns with universal SIMD acceleration
**Performance**: 2.7-4× speedup, 37% storage reduction
