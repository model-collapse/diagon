# Sparse Vector Search Implementation Plan

## Overview

Implement sparse vector search in DIAGON with support for two retrieval algorithms:
1. **QBlock**: Block-based quantized inverted index (from BitQ paper/implementation)
2. **SINDI**: SIMD-optimized inverted index (from SINDI paper: https://arxiv.org/html/2509.08395v2)

## Background

### Sparse Vectors

Sparse vectors represent high-dimensional vectors where most values are zero:
- **Representation**: List of (index, value) pairs
- **Use cases**: Text embeddings (SPLADE, BM25 expansion), learned sparse retrieval
- **Advantages**: Memory efficient, interpretable, fast exact search

### QBlock Algorithm (BitQ)

**Key concepts**:
- **Quantization**: Map continuous weights to discrete blocks (bins)
- **Block structure**: Inverted index organized by (term, quantization_block, window)
- **Window partitioning**: Divide documents into fixed-size windows (e.g., 100K docs)
- **Block selection**: Rank blocks by gain (block_size × query_weight × block_id)
- **Scatter-add**: Accumulate scores from selected blocks

**Data structures**:
```cpp
// Quantized inverted index: term -> block_id -> window_id -> doc_list
using QuantizedBlock = struct {
    std::vector<doc_id_t> documents;
    std::vector<uint8_t> weights;  // Optional for weighted scoring
};

using QuantizedIndex =
    std::vector<                          // [term]
        std::vector<                      // [block_id]
            std::vector<QuantizedBlock>   // [window_id]
        >
    >;
```

**Query algorithm**:
1. For each query term, compute block gains
2. Select top-k blocks by gain
3. Iterate selected blocks, accumulate scores (scatter-add)
4. Rerank top-k' candidates with exact scores
5. Return top-k results

### SINDI Algorithm

**Key concepts** (from paper):
- **SIMD-optimized** block-max WAND algorithm
- **Unified storage**: Store postings in SIMD-friendly layout
- **Block-max scores**: Precompute maximum score per block
- **WAND pruning**: Skip blocks that can't contribute to top-k

**Optimizations**:
- AVX2/AVX-512 vectorized score accumulation
- Cache-friendly data layout
- Prefetching hints

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────┐
│                  Sparse Vector Search                    │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌──────────────────────┐    ┌──────────────────────┐  │
│  │  SparseVectorField   │    │  SparseVectorQuery   │  │
│  │  (Document API)      │    │  (Query API)         │  │
│  └──────────────────────┘    └──────────────────────┘  │
│             │                          │                 │
│             └──────────┬───────────────┘                 │
│                        │                                 │
│           ┌────────────▼──────────────┐                 │
│           │   SparseVectorIndex       │                 │
│           │   (Configurable backend)  │                 │
│           ├───────────────────────────┤                 │
│           │ • QBlockIndex (BitQ)      │                 │
│           │ • SindiIndex (SIMD-opt)   │                 │
│           └───────────────────────────┘                 │
│                        │                                 │
│           ┌────────────▼──────────────┐                 │
│           │   Column Storage          │                 │
│           │   (Reuse existing)        │                 │
│           ├───────────────────────────┤                 │
│           │ • ColumnVector<uint32_t>  │ // indices       │
│           │ • ColumnVector<float>     │ // values        │
│           │ • Compression (LZ4/ZSTD)  │                 │
│           └───────────────────────────┘                 │
│                                                           │
└─────────────────────────────────────────────────────────┘
```

## Implementation Plan

### Phase 1: Core Data Structures (Week 1)

#### Task 1.1: SparseVector Data Type

**Files to create**:
- `src/core/include/diagon/sparse/SparseVector.h`
- `src/core/src/sparse/SparseVector.cpp`

**Implementation**:
```cpp
namespace diagon::sparse {

// Sparse vector element (index, value pair)
struct SparseElement {
    uint32_t index;  // Dimension index
    float value;     // Weight/score

    SparseElement() : index(0), value(0.0f) {}
    SparseElement(uint32_t idx, float val) : index(idx), value(val) {}

    bool operator<(const SparseElement& other) const {
        return index < other.index;
    }
};

// Sparse vector: sorted list of (index, value) pairs
class SparseVector {
public:
    SparseVector() = default;

    // Add element (maintains sorted order)
    void add(uint32_t index, float value);

    // Access
    size_t size() const { return elements_.size(); }
    const SparseElement& operator[](size_t i) const { return elements_[i]; }

    // Iteration
    auto begin() const { return elements_.begin(); }
    auto end() const { return elements_.end(); }

    // Dot product with another sparse vector
    float dot(const SparseVector& other) const;

    // L2 norm
    float norm() const;

    // Top-k pruning (keep only top k elements by value)
    void prune(size_t k);

    // Alpha-mass pruning (keep elements covering alpha% of total mass)
    void pruneByMass(float alpha);

private:
    std::vector<SparseElement> elements_;
};

}  // namespace diagon::sparse
```

**Complexity**: ~200 lines

#### Task 1.2: SparseVectorField (Document API)

**Files to create**:
- `src/core/include/diagon/document/SparseVectorField.h`
- `src/core/src/document/SparseVectorField.cpp`

**Implementation**:
```cpp
namespace diagon::document {

class SparseVectorField : public IndexableField {
public:
    SparseVectorField(const std::string& name,
                      const sparse::SparseVector& vector,
                      bool stored = false);

    // IndexableField interface
    std::string name() const override { return name_; }
    FieldType fieldType() const override { return FieldType::SPARSE_VECTOR; }

    // Sparse vector access
    const sparse::SparseVector& sparseVector() const { return vector_; }

private:
    std::string name_;
    sparse::SparseVector vector_;
    bool stored_;
};

}  // namespace diagon::document
```

**Complexity**: ~100 lines

### Phase 2: QBlock Index Implementation (Week 2)

#### Task 2.1: Quantization Infrastructure

**Files to create**:
- `src/core/include/diagon/sparse/Quantizer.h`
- `src/core/src/sparse/Quantizer.cpp`

**Implementation**:
```cpp
namespace diagon::sparse {

class Quantizer {
public:
    // Fixed-bin quantizer (e.g., 256 bins)
    explicit Quantizer(int num_bins = 256,
                      float min_weight = 0.0f,
                      float max_weight = 3.0f);

    // Quantize weight to bin ID
    int quantize(float weight) const;

    // Dequantize bin ID to approximate weight
    float dequantize(int bin_id) const;

    // Get number of bins
    int numBins() const { return num_bins_; }

private:
    int num_bins_;
    float min_weight_;
    float max_weight_;
    float scale_;  // (max - min) / num_bins
};

}  // namespace diagon::sparse
```

**Complexity**: ~150 lines

#### Task 2.2: QBlock Index Structure

**Files to create**:
- `src/core/include/diagon/sparse/QBlockIndex.h`
- `src/core/src/sparse/QBlockIndex.cpp`

**Implementation**:
```cpp
namespace diagon::sparse {

class QBlockIndex {
public:
    // Configuration
    struct Config {
        int num_bins = 256;          // Number of quantization bins
        int window_size = 100000;    // Documents per window
        float doc_cut_alpha = 1.0f;  // Document pruning (1.0 = no pruning)
        int num_threads = 8;         // Parallel indexing threads
    };

    // Quantized block: list of documents in this (term, block, window)
    struct QuantizedBlock {
        std::vector<uint32_t> documents;  // Local doc IDs (within window)
        std::vector<uint8_t> weights;     // Optional: original quantized weights
    };

    // Constructor
    explicit QBlockIndex(const Config& config);

    // Build index from documents
    void build(const std::vector<sparse::SparseVector>& documents);

    // Search top-k similar vectors
    std::vector<SearchResult> search(const sparse::SparseVector& query,
                                     int k,
                                     const SearchOptions& options) const;

    // Persistence
    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    Config config_;
    Quantizer quantizer_;

    // Index structure: [term][block_id][window_id] -> QuantizedBlock
    using IndexStructure =
        std::vector<                              // [term]
            std::vector<                          // [block_id]
                std::vector<QuantizedBlock>       // [window_id]
            >
        >;
    IndexStructure index_;

    // Block sizes: [term][block_id] -> total_size across all windows
    std::vector<std::vector<int32_t>> block_sizes_;

    // Forward index (for reranking)
    std::vector<sparse::SparseVector> forward_index_;

    int num_windows_;
    int max_dimension_;

    // Index building
    void buildWorker(size_t start_doc, size_t end_doc,
                    const std::vector<sparse::SparseVector>& documents,
                    std::vector<std::mutex>& dim_mutexes);

    // Query processing
    struct BlockWithScore {
        const std::vector<QuantizedBlock>* blocks;
        uint32_t gain;  // block_size × query_weight × block_id
        uint32_t term;
        uint32_t block_id;
        float query_weight;
    };

    std::vector<BlockWithScore> selectBlocks(
        const sparse::SparseVector& query,
        const SearchOptions& options) const;

    void scatterAdd(const std::vector<BlockWithScore>& blocks,
                   std::vector<float>& scores,
                   TopKHeap<SearchResult>& top_k) const;

    void rerank(const sparse::SparseVector& query,
               TopKHeap<SearchResult>& candidates,
               int k) const;
};

}  // namespace diagon::sparse
```

**Complexity**: ~500 lines

### Phase 3: SINDI Index Implementation (Week 3)

#### Design Corrections

**Key improvements over initial plan**:
1. **Use ColumnVector** for storage (not std::vector) - enables mmap
2. **Leverage MMapDirectory** for zero-copy access
3. **Add prefetch** from existing SIMDUtils.h
4. **Unified format** with QBlock (same .col files)

#### Task 3.1: SIMD Score Accumulation with Prefetch

**Files to create**:
- `src/core/include/diagon/sparse/SindiScorer.h`
- `src/core/src/sparse/SindiScorer.cpp`

**Implementation**:
```cpp
namespace diagon::sparse {

class SindiScorer {
public:
    // SIMD-optimized dot product accumulation with prefetch
    // Uses AVX2 for 8x float processing
    static void accumulateScoresAVX2(
        const uint32_t* doc_ids,
        const float* doc_weights,
        size_t count,
        float query_weight,
        std::vector<float>& scores,
        bool use_prefetch = true);

    // Scalar fallback
    static void accumulateScoresScalar(
        const uint32_t* doc_ids,
        const float* doc_weights,
        size_t count,
        float query_weight,
        std::vector<float>& scores);

    // Dispatch to best available implementation
    static void accumulateScores(
        const uint32_t* doc_ids,
        const float* doc_weights,
        size_t count,
        float query_weight,
        std::vector<float>& scores,
        bool use_simd = true,
        bool use_prefetch = true);

private:
    // Prefetch distance (elements ahead)
    static constexpr size_t PREFETCH_DISTANCE = 8;
};

}  // namespace diagon::sparse
```

**Key optimization**: Prefetch next cache line while processing current:
```cpp
for (size_t i = 0; i < count; i += 8) {
    // Prefetch next iteration
    if (i + PREFETCH_DISTANCE < count) {
        util::simd::Prefetch::read(&doc_ids[i + PREFETCH_DISTANCE]);
        util::simd::Prefetch::read(&doc_weights[i + PREFETCH_DISTANCE]);
    }

    // Process current with AVX2
    __m256i ids = _mm256_loadu_si256((__m256i*)&doc_ids[i]);
    __m256 weights = _mm256_loadu_ps(&doc_weights[i]);
    // ... accumulation ...
}
```

**Complexity**: ~350 lines

#### Task 3.2: SINDI Index Structure with ColumnVector

**Files to create**:
- `src/core/include/diagon/sparse/SindiIndex.h`
- `src/core/src/sparse/SindiIndex.cpp`

**Implementation** (leveraging ColumnVector + MMap):
```cpp
namespace diagon::sparse {

class SindiIndex {
public:
    struct Config {
        int block_size = 128;        // Documents per block
        bool use_block_max = true;   // Enable block-max WAND
        bool use_simd = true;        // Enable SIMD acceleration
        bool use_mmap = true;        // Enable mmap for index files
        bool use_prefetch = true;    // Enable prefetch hints
        int chunk_power = 30;        // MMap chunk size (1GB default)
    };

    // Block metadata (stored separately)
    struct BlockMetadata {
        uint32_t offset;      // Offset in doc_ids/weights arrays
        uint32_t count;       // Number of docs in block
        float max_weight;     // For block-max WAND
    };

    explicit SindiIndex(const Config& config);

    // Build index from documents
    void build(const std::vector<sparse::SparseVector>& documents);

    // Load existing index from directory
    void load(store::Directory* directory, const std::string& segment);

    // Save index to directory
    void save(store::Directory* directory, const std::string& segment);

    // Search with SIMD acceleration and WAND pruning
    std::vector<SearchResult> search(const sparse::SparseVector& query,
                                     int k) const;

private:
    Config config_;

    // Posting lists: [term] -> ColumnVector (can be mmap'd)
    // Using ColumnVector allows:
    // - Zero-copy mmap access via MMapDirectory
    // - Compression (LZ4/ZSTD) if needed
    // - Consistent format with QBlock
    std::vector<std::shared_ptr<columns::ColumnVector<uint32_t>>> term_doc_ids_;
    std::vector<std::shared_ptr<columns::ColumnVector<float>>> term_weights_;

    // Block metadata: [term] -> [block_metadata]
    std::vector<std::vector<BlockMetadata>> term_blocks_;

    // Maximum weights per term (for WAND upper bound)
    std::vector<float> max_term_weights_;

    // MMap directory for loading index (optional)
    std::unique_ptr<store::MMapDirectory> mmap_dir_;

    // Query processing with WAND
    std::vector<SearchResult> searchWithWand(
        const sparse::SparseVector& query,
        int k) const;

    // Block-max WAND optimization
    float computeUpperBound(const std::vector<size_t>& term_indices,
                           const std::vector<float>& query_weights,
                           size_t skip_term) const;
};

}  // namespace diagon::sparse
```

**Storage format** (unified with QBlock):
```
<index_dir>/
├── _0.sindi/
│   ├── doc_ids_term_0.col      # ColumnVector<uint32_t> for term 0
│   ├── weights_term_0.col      # ColumnVector<float> for term 0
│   ├── doc_ids_term_1.col
│   ├── weights_term_1.col
│   ├── ...
│   ├── block_meta.bin          # Block metadata
│   └── term_stats.bin          # Max weights per term
```

**Complexity**: ~500 lines

### Phase 4: Integration & Testing (Week 4)

#### Task 4.1: IndexWriter Integration

**Modifications**:
- `src/core/include/diagon/index/IndexWriter.h`
- `src/core/src/index/IndexWriter.cpp`

**Changes**:
```cpp
// Add sparse vector field handling in addDocument()
void IndexWriter::addDocument(const Document& doc) {
    for (const auto& field : doc.fields()) {
        if (field->fieldType() == FieldType::SPARSE_VECTOR) {
            auto* sparse_field = static_cast<const SparseVectorField*>(field.get());
            // Add to sparse vector index (QBlock or SINDI)
            sparseVectorWriter_->addVector(field->name(),
                                          sparse_field->sparseVector());
        }
    }
}
```

#### Task 4.2: Query API

**Files to create**:
- `src/core/include/diagon/search/SparseVectorQuery.h`
- `src/core/src/search/SparseVectorQuery.cpp`

**Implementation**:
```cpp
namespace diagon::search {

class SparseVectorQuery : public Query {
public:
    // Query configuration
    enum class Algorithm {
        QBLOCK,   // QBlock/BitQ algorithm
        SINDI     // SINDI SIMD-optimized
    };

    struct Options {
        Algorithm algorithm = Algorithm::QBLOCK;
        int k = 10;                  // Top-k results
        int kprime = 100;            // Candidates for reranking
        int block_budget = 1000;     // Max blocks to process (QBlock)
        float alpha = 0.8f;          // Query pruning threshold
    };

    SparseVectorQuery(const std::string& field,
                     const sparse::SparseVector& query_vector,
                     const Options& options = Options());

    // Query interface
    std::unique_ptr<Weight> createWeight(IndexSearcher* searcher) override;
    std::string toString() const override;

private:
    std::string field_;
    sparse::SparseVector query_vector_;
    Options options_;
};

}  // namespace diagon::search
```

**Complexity**: ~300 lines

#### Task 4.3: Benchmarks

**Files to create**:
- `benchmarks/SparseVectorSearchBenchmark.cpp`

**Tests**:
```cpp
// Benchmark 1: Index construction
static void BM_SparseVector_IndexBuild_QBlock(benchmark::State& state);
static void BM_SparseVector_IndexBuild_SINDI(benchmark::State& state);

// Benchmark 2: Query latency
static void BM_SparseVector_Query_QBlock(benchmark::State& state);
static void BM_SparseVector_Query_SINDI(benchmark::State& state);

// Benchmark 3: k-NN recall
static void BM_SparseVector_Recall_QBlock(benchmark::State& state);
static void BM_SparseVector_Recall_SINDI(benchmark::State& state);
```

#### Task 4.4: Integration Tests

**Files to create**:
- `tests/integration/SparseVectorSearchTest.cpp`

**Tests**:
- Index build and search end-to-end
- Correctness: Compare QBlock vs SINDI vs brute-force
- Recall@10, Recall@100
- Query latency
- Index size

## Expected Performance

### QBlock (BitQ) Algorithm

**Characteristics**:
- **Query latency**: 1-5ms (depends on block budget)
- **Recall@10**: 95%+ (with sufficient blocks)
- **Index overhead**: 2-3× vs raw vectors (quantization + blocking)
- **Speedup vs brute-force**: 10-100× (depends on dataset)

**Trade-offs**:
- Higher recall requires more blocks (slower)
- Quantization introduces approximation error
- Block selection heuristic may miss relevant documents

### SINDI Algorithm

**Characteristics**:
- **Query latency**: 0.5-2ms (SIMD acceleration)
- **Recall@10**: 99%+ (exact with full WAND)
- **Index overhead**: 1.5-2× vs raw vectors
- **Speedup vs brute-force**: 5-20× (SIMD + WAND pruning)

**Trade-offs**:
- WAND guarantees exact top-k (no approximation)
- SIMD requires AVX2/AVX-512 support
- Less aggressive pruning than QBlock

## Configuration Example

```cpp
// Example: Index sparse vectors with QBlock
IndexWriterConfig config;
config.setSparseVectorAlgorithm(SparseVectorQuery::Algorithm::QBLOCK);
config.setSparseVectorConfig({
    .num_bins = 256,
    .window_size = 100000,
    .doc_cut_alpha = 0.9f
});

auto writer = IndexWriter::create(dir.get(), config);

// Add documents with sparse vectors
Document doc;
doc.addField(std::make_unique<TextField>("title", "..."));

sparse::SparseVector vec;
vec.add(10, 0.8f);
vec.add(25, 1.2f);
vec.add(100, 0.5f);
doc.addField(std::make_unique<SparseVectorField>("embedding", vec));

writer->addDocument(doc);
writer->commit();

// Search
auto reader = DirectoryReader::open(dir.get());
IndexSearcher searcher(reader.get());

sparse::SparseVector query_vec;
query_vec.add(10, 0.9f);
query_vec.add(25, 1.1f);

SparseVectorQuery::Options opts;
opts.algorithm = SparseVectorQuery::Algorithm::QBLOCK;
opts.k = 10;
opts.kprime = 100;

auto query = SparseVectorQuery("embedding", query_vec, opts);
auto results = searcher.search(&query, 10);
```

## Files Summary

### New Files (17 total)

**Core data structures** (4 files):
1. `src/core/include/diagon/sparse/SparseVector.h`
2. `src/core/src/sparse/SparseVector.cpp`
3. `src/core/include/diagon/document/SparseVectorField.h`
4. `src/core/src/document/SparseVectorField.cpp`

**QBlock implementation** (4 files):
5. `src/core/include/diagon/sparse/Quantizer.h`
6. `src/core/src/sparse/Quantizer.cpp`
7. `src/core/include/diagon/sparse/QBlockIndex.h`
8. `src/core/src/sparse/QBlockIndex.cpp`

**SINDI implementation** (4 files):
9. `src/core/include/diagon/sparse/SindiScorer.h`
10. `src/core/src/sparse/SindiScorer.cpp`
11. `src/core/include/diagon/sparse/SindiIndex.h`
12. `src/core/src/sparse/SindiIndex.cpp`

**Query API** (2 files):
13. `src/core/include/diagon/search/SparseVectorQuery.h`
14. `src/core/src/search/SparseVectorQuery.cpp`

**Testing** (3 files):
15. `tests/integration/SparseVectorSearchTest.cpp`
16. `tests/unit/sparse/SparseVectorTest.cpp`
17. `benchmarks/SparseVectorSearchBenchmark.cpp`

### Modified Files (2)
- `src/core/include/diagon/index/IndexWriter.h`
- `src/core/src/index/IndexWriter.cpp`

### Total Code Estimate
- New code: ~2,400 lines
- Modified code: ~100 lines
- **Total: ~2,500 lines**

## Timeline

- **Week 1**: Core data structures (SparseVector, SparseVectorField)
- **Week 2**: QBlock index implementation
- **Week 3**: SINDI index implementation
- **Week 4**: Integration, testing, benchmarks

**Total: 4 weeks for 1 developer**

## Success Criteria

**Correctness**:
- [ ] Sparse vector dot product matches brute-force
- [ ] QBlock recall@10 > 95%
- [ ] SINDI recall@10 > 99%

**Performance**:
- [ ] QBlock query latency < 5ms
- [ ] SINDI query latency < 2ms
- [ ] Index build throughput > 10K docs/sec

**Integration**:
- [ ] Can index documents with sparse vector fields
- [ ] Can query sparse vectors via SparseVectorQuery
- [ ] All tests passing

## References

- **QBlock/BitQ**: Reference implementation at `/home/ubuntu/bitq-code/cpp-sparse-ann/`
- **SINDI paper**: https://arxiv.org/html/2509.08395v2
- **SPLADE**: Learned sparse retrieval model
- **Block-Max WAND**: Efficient top-k retrieval algorithm
