# Direct Document Retrieval in BlockMaxQuantizedIndex

## Overview

BlockMaxQuantizedIndex now supports **direct document retrieval** by document ID. This feature allows downstream consumers to:

1. Query the index to get document IDs (top-k results)
2. Retrieve the actual document content (sparse vectors) for those IDs

## API

### Single Document Retrieval

```cpp
const SparseDoc& getDocument(doc_id_t doc_id) const;
```

Returns the sparse document by ID from the forward index.

**Throws**: `std::out_of_range` if doc_id >= numDocuments()

**Example**:
```cpp
BlockMaxQuantizedIndex index;
// ... build index ...

// Query to get document IDs
auto result_ids = index.query(query, params);

// Retrieve first result document
const SparseDoc& doc = index.getDocument(result_ids[0]);

// Access document terms
for (const auto& elem : doc) {
    std::cout << "Term: " << elem.term << ", Score: " << elem.score << std::endl;
}
```

### Batch Document Retrieval

```cpp
std::vector<SparseDoc> getDocuments(const std::vector<doc_id_t>& doc_ids) const;
```

Returns multiple documents by their IDs in a single call.

**Note**: Invalid doc IDs return empty documents (no exception thrown for batch operations)

**Example**:
```cpp
// Get top-10 document IDs from query
auto result_ids = index.query(query, params);

// Retrieve all top-10 documents in batch
auto documents = index.getDocuments(result_ids);

// Process retrieved documents
for (size_t i = 0; i < documents.size(); ++i) {
    std::cout << "Doc " << result_ids[i] << " has " << documents[i].size() << " terms\n";
}
```

## Performance

From benchmark on MSMarco v1 SPLADE (1000 documents):

| Operation | Latency | Throughput |
|-----------|---------|------------|
| **Single retrieval** | 0.16 µs | ~6.25M docs/sec |
| **Batch (3 docs)** | 0.31 µs/doc | ~3.23M docs/sec |

**Key characteristics**:
- **O(1) lookup**: Direct array access to forward index
- **Zero-copy return**: Returns const reference (single doc) or copies (batch)
- **Extremely fast**: Sub-microsecond latency for typical documents

## Use Cases

### 1. Query + Retrieve Pipeline

```cpp
// Phase 1: Fast approximate search
auto candidate_ids = index.query(user_query, params);

// Phase 2: Retrieve full documents for downstream processing
auto candidates = index.getDocuments(candidate_ids);

// Phase 3: Advanced reranking, highlighting, or display
for (size_t i = 0; i < candidates.size(); ++i) {
    // Complex reranking logic
    float reranked_score = advancedScorer(user_query, candidates[i]);
    // Highlighting
    std::string snippet = generateSnippet(candidates[i], user_query);
}
```

### 2. Document Export/Serialization

```cpp
// Export top results for external processing
auto top_docs = index.getDocuments(result_ids);

// Serialize to JSON, protobuf, etc.
for (const auto& doc : top_docs) {
    json j = serializeSparseDoc(doc);
    output_stream << j << "\n";
}
```

### 3. Feature Extraction

```cpp
// Extract features from retrieved documents
auto docs = index.getDocuments(doc_ids);

for (const auto& doc : docs) {
    // Extract term statistics
    size_t num_terms = doc.size();
    float max_score = 0.0f;
    for (const auto& elem : doc) {
        max_score = std::max(max_score, elem.score);
    }

    // Use for ML features
    features.push_back({num_terms, max_score});
}
```

### 4. Debugging and Analysis

```cpp
// Inspect why a document was retrieved
auto result_ids = index.query(query, params);

std::cout << "Top 3 results:\n";
for (int i = 0; i < 3 && i < result_ids.size(); ++i) {
    const auto& doc = index.getDocument(result_ids[i]);

    std::cout << "Doc " << result_ids[i] << ":\n";
    std::cout << "  Terms: " << doc.size() << "\n";
    std::cout << "  Top terms: ";

    // Show highest-scoring terms
    auto sorted_doc = doc;
    std::sort(sorted_doc.begin(), sorted_doc.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });

    for (int j = 0; j < 5 && j < sorted_doc.size(); ++j) {
        std::cout << sorted_doc[j].term << "(" << sorted_doc[j].score << ") ";
    }
    std::cout << "\n";
}
```

## Implementation Details

### Forward Index Storage

Documents are stored in a forward index (`std::vector<SparseDoc>`) during index build:

```cpp
void build(const std::vector<SparseDoc>& documents) {
    // ... build inverted index ...

    // Store forward index for retrieval and reranking
    forward_index_ = documents;
}
```

**Memory overhead**:
- Stores full sparse document for each indexed document
- Example: 8.8M documents × ~120 terms/doc × 8 bytes = ~8.4 GB

### Retrieval Implementation

**Single document**:
```cpp
const SparseDoc& getDocument(doc_id_t doc_id) const {
    if (doc_id >= forward_index_.size()) {
        throw std::out_of_range("Document ID out of range");
    }
    return forward_index_[doc_id];  // O(1) array access
}
```

**Batch retrieval**:
```cpp
std::vector<SparseDoc> getDocuments(const std::vector<doc_id_t>& doc_ids) const {
    std::vector<SparseDoc> result;
    result.reserve(doc_ids.size());

    for (doc_id_t doc_id : doc_ids) {
        if (doc_id < forward_index_.size()) {
            result.push_back(forward_index_[doc_id]);  // Copy
        } else {
            result.push_back(SparseDoc());  // Empty for invalid IDs
        }
    }

    return result;
}
```

## Error Handling

### Single Document

**Throws exception** for invalid doc IDs:
```cpp
try {
    const auto& doc = index.getDocument(999999);
} catch (const std::out_of_range& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    // Output: "Document ID 999999 is out of range (max: 999)"
}
```

### Batch Retrieval

**Returns empty documents** for invalid IDs (no exception):
```cpp
std::vector<doc_id_t> ids = {10, 999999, 20};  // Middle ID is invalid
auto docs = index.getDocuments(ids);

// Result: [valid_doc_10, empty_doc, valid_doc_20]
assert(docs.size() == 3);
assert(docs[1].empty());  // Invalid ID returns empty document
```

**Rationale**: Batch operations don't throw to allow partial results. Check for empty documents if validation is needed.

## Comparison with QBlock

### QBlock Approach
- Stores CSR matrix directly: `CsrMatrix sparse_vecs`
- No explicit document retrieval API
- Accesses CSR rows directly during reranking

### DIAGON Approach
- Stores `std::vector<SparseDoc>` forward index
- Explicit retrieval API: `getDocument()`, `getDocuments()`
- Cleaner API separation: query → retrieve → process

**Trade-off**:
- DIAGON: Easier API, more memory flexible
- QBlock: More memory efficient (CSR is compressed)

## Memory Considerations

### Forward Index Size

For typical sparse vectors:
- MSMarco v1 SPLADE: ~120 terms/doc average
- Memory per doc: 120 terms × 8 bytes (term + score) = 960 bytes
- 1M docs: ~960 MB
- 10M docs: ~9.6 GB

### Optimization Options

**Option 1: Disable forward index** (not currently supported):
```cpp
// Future API
Config config;
config.store_forward_index = false;  // Save memory
```
**Limitation**: Can't retrieve documents or do exact reranking

**Option 2: Compressed storage** (future work):
```cpp
// Store in compressed CSR format instead of SparseDoc vectors
// Trade CPU (decompression) for memory
```

**Option 3: External storage** (future work):
```cpp
// Store documents on disk, memory-map on access
// Trade I/O for memory
```

## Testing

### Benchmark Test Output

```
========================================
Testing Direct Document Retrieval
========================================
Running sample query to get document IDs...
  Found 5 results

Testing single document retrieval:
  Doc ID: 245
  Num terms: 144
  Retrieval time: 0.16 µs
  First 5 terms: (2001,0.523827) (2002,0.558499) (2010,0.722023) ...

Testing batch document retrieval:
  Batch size: 3
  Retrieved: 3 documents
  Batch retrieval time: 0.93 µs
  Avg per doc: 0.31 µs
  Total terms retrieved: 451

Testing error handling:
  Attempting to retrieve invalid doc ID 2000...
  ✓ Correctly threw exception: Document ID 2000 is out of range (max: 999)
========================================
```

## Summary

**Direct document retrieval** addresses the downstream requirement for accessing indexed documents:
- ✅ **Fast**: Sub-microsecond latency
- ✅ **Simple API**: Single and batch retrieval
- ✅ **Type-safe**: Returns const reference or vector
- ✅ **Error handling**: Exceptions for single, empty docs for batch
- ✅ **Tested**: Integrated into benchmark

**Use it when you need**:
- Full document content after querying
- Advanced reranking with original term weights
- Document export or serialization
- Feature extraction for ML
- Debugging and analysis

---

**Implementation Date**: 2026-01-26
**Feature Status**: ✅ Complete
**API Stability**: Stable
**Performance**: Production-ready
