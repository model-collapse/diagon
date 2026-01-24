# Lucene++ Design Overview

## Executive Summary

Lucene++ is a C++ implementation of a search engine library combining Lucene's inverted index architecture with ClickHouse's columnar storage capabilities. It provides both full-text search and analytical query capabilities with flexible storage modes (in-memory vs disk-based) and multi-tier storage support.

## Design Goals

1. **Hybrid Storage Architecture**: Support both inverted indexes (for search) and columnar storage (for analytics)
2. **Flexible Term Dictionary**: Pluggable term index implementations (trie-tree for prefix queries, hash map for exact lookups)
3. **Memory Flexibility**: Support both full in-memory and disk-based modes with memory-mapped files
4. **Storage Tiering**: Hot/warm/cold tier architecture inspired by OpenSearch
5. **Type-Aware Columns**: Dynamic column storage with type-level partitioning per ClickHouse
6. **Lucene-Compatible API**: Library-level interface aligned with Apache Lucene
7. **Extensibility**: Designed for circuit breakers, custom codecs, and query extensions

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Lucene++ Library                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐              ┌──────────────────────┐   │
│  │   IndexWriter    │              │    IndexReader       │   │
│  │                  │              │                      │   │
│  │  - Document Add  │              │  - Query Execution   │   │
│  │  - Field Analysis│              │  - Search/Scan       │   │
│  │  - Segment Build │              │  - Column Access     │   │
│  └────────┬─────────┘              └──────────┬───────────┘   │
│           │                                    │               │
│  ┌────────▼────────────────────────────────────▼──────────┐   │
│  │              Segment Management                         │   │
│  │  - Segment Creation    - Merging    - Tier Migration   │   │
│  └────────┬────────────────────────────────────┬──────────┘   │
│           │                                    │               │
│  ┌────────▼───────────┐            ┌──────────▼──────────┐   │
│  │  Inverted Index    │            │  Column Storage      │   │
│  │                    │            │                      │   │
│  │ ┌───────────────┐  │            │ ┌─────────────────┐ │   │
│  │ │ Term Dict     │  │            │ │ Type Partitions │ │   │
│  │ │ - Trie/Hash   │  │            │ │ - Int32/Int64   │ │   │
│  │ └───────────────┘  │            │ │ - Float/Double  │ │   │
│  │ ┌───────────────┐  │            │ │ - String/Bytes  │ │   │
│  │ │ Posting Lists │  │            │ │ - Array/Nested  │ │   │
│  │ │ - Compressed  │  │            │ └─────────────────┘ │   │
│  │ │ - Skip Lists  │  │            │ ┌─────────────────┐ │   │
│  │ └───────────────┘  │            │ │ Granule Index   │ │   │
│  └────────────────────┘            │ │ - Sparse Index  │ │   │
│                                    │ │ - Mark Files    │ │   │
│                                    │ └─────────────────┘ │   │
│                                    └─────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            Storage Layer (Tiered)                    │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐           │   │
│  │  │   Hot    │  │   Warm   │  │   Cold   │           │   │
│  │  │ (Memory) │  │  (SSD)   │  │  (HDD/S3)│           │   │
│  │  └──────────┘  └──────────┘  └──────────┘           │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │         Memory Management & I/O                      │   │
│  │  - Arena Allocators  - mmap  - Direct I/O           │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Index Writer
- Document ingestion and field analysis
- Segment building (in-memory buffer → flush to disk)
- Field type detection and routing (inverted index vs column storage)
- Merge policy execution

### 2. Index Reader
- Multi-segment searching
- Query execution (term queries, phrase queries, range queries)
- Column scanning for analytical queries
- Tier-aware data access

### 3. Term Dictionary
- **Pluggable implementations**:
  - **Trie (FST)**: Prefix queries, fuzzy matching, sorted iteration
  - **Hash Map**: Exact term lookup, lower memory overhead
- Memory-mapped or in-memory storage

### 4. Posting Lists
- Document ID lists with term frequencies and positions
- Compression: delta encoding, variable-length integers, bit packing
- Skip lists for efficient intersection/union
- Support for term vectors and payloads

### 5. Column Storage
- **Type-partitioned columns**: Separate storage per data type
- **Granule-based organization**: ClickHouse-inspired chunking (8K-16K rows)
- **Sparse indexing**: Primary key index on granule boundaries
- **Compression**: Type-specific codecs (LZ4, ZSTD, RLE, FOR)

### 6. Segment Management
- Immutable segments (Lucene-style)
- Background merging with configurable policies
- Tier migration (hot → warm → cold)
- Lock-free segment visibility

### 7. Storage Layer
- **Hot Tier**: In-memory or memory-mapped on SSD (recent/active data)
- **Warm Tier**: Disk-based on SSD (older searchable data)
- **Cold Tier**: HDD or S3 (archived, rarely accessed)
- Transparent tier migration based on age/access patterns

### 8. Memory Management
- **In-Memory Mode**: Arena allocators, zero-copy reads
- **Disk Mode**: mmap for read-mostly data, direct I/O for writes
- Unified buffer pool with LRU eviction
- Circuit breaker integration points (memory limits, query complexity)

## Data Structures

### Segment File Layout

```
segment_N/
├── _metadata.json              # Segment metadata (schema, statistics)
├── inverted_index/
│   ├── _term_dict.trie/.hash   # Term dictionary (pluggable)
│   ├── _postings.dat           # Posting lists (compressed)
│   ├── _postings.skip          # Skip list data
│   └── _term_vectors.dat       # Term positions (optional)
├── column_storage/
│   ├── field_name.int32/
│   │   ├── data.bin            # Compressed column data
│   │   ├── marks.idx           # Granule offsets
│   │   └── primary.idx         # Sparse primary index
│   ├── field_name.string/
│   │   ├── data.bin
│   │   ├── dict.bin            # Dictionary for low-cardinality strings
│   │   ├── marks.idx
│   │   └── primary.idx
│   └── field_name.float64/
│       └── ...
├── doc_values/                 # Fast random access to field values
│   └── field_name.dv
└── deleted_docs.bm             # Deleted document bitmap
```

### In-Memory Structures

```cpp
// Segment (immutable after commit)
struct Segment {
    std::string name;
    uint32_t doc_count;
    uint32_t deleted_count;
    StorageTier tier;

    // Inverted index
    std::unique_ptr<TermDictionary> term_dict;  // Trie or HashMap
    std::unique_ptr<PostingLists> postings;

    // Column storage
    std::unordered_map<std::string, ColumnStorage> columns;

    // Metadata
    FieldInfos field_infos;
    SegmentInfo segment_info;
};

// Document representation
struct Document {
    uint32_t doc_id;
    std::unordered_map<std::string, Field> fields;
};

// Field with type information
struct Field {
    std::string name;
    FieldType type;
    FieldStorage storage;  // INVERTED_INDEX | COLUMN_STORAGE | DOC_VALUES
    std::variant<int32_t, int64_t, float, double, std::string, bytes> value;
};
```

## Design Decisions

### 1. Term Dictionary: Trie vs Hash Map

**Trie (FST - Finite State Transducer)**:
- **Pros**: Prefix queries, fuzzy search, memory-efficient for common prefixes, sorted iteration
- **Cons**: Slower exact lookups, complex implementation
- **Use Case**: Text search, wildcard queries, autocomplete

**Hash Map (std::unordered_map or robin_hood)**:
- **Pros**: O(1) exact lookups, simpler implementation
- **Cons**: No prefix queries, higher memory for sparse terms
- **Use Case**: Structured data, exact-match queries, keyword fields

**Implementation Strategy**:
- Per-field configuration
- Default: Trie for text fields, hash map for keyword fields
- Runtime switching via field schema

### 2. Memory vs Disk Mode

**In-Memory Mode**:
- All segments in RAM
- Arena allocators for zero-copy
- Best for: Small indexes (<10GB), latency-sensitive workloads

**Disk Mode (mmap)**:
- Memory-mapped segment files
- OS manages paging
- Best for: Large indexes, read-heavy workloads

**Hybrid Mode**:
- Hot tier in-memory, warm/cold on disk
- Configurable memory budget with LRU eviction

### 3. Column Storage Type Partitioning

Following ClickHouse's approach, each field with column storage creates separate physical columns per type:

```
user_id (dynamic field):
  - user_id.int32/      # Stores integer IDs
  - user_id.string/     # Stores string IDs (e.g., "user_12345")
  - user_id.null.bm     # Null bitmap

metadata (nested):
  - metadata.tags.array.string/
  - metadata.score.float64/
```

**Benefits**:
- Type-specific compression
- Efficient type-based filtering
- Schema evolution without migration

### 4. Storage Tiers

```
Hot Tier (0-7 days):
  - In-memory or memory-mapped on NVMe
  - All indexes loaded
  - Sub-millisecond latency

Warm Tier (7-90 days):
  - Disk-based on SSD
  - Selective index loading
  - <10ms latency

Cold Tier (>90 days):
  - HDD or S3
  - Load on demand
  - <100ms latency (acceptable for archival queries)
```

**Transition Policy**:
- Automatic based on segment age
- Manual override via API
- Merge during transition (compact small segments)

## API Design (Lucene-Compatible)

### Index Writing

```cpp
// Create index writer
IndexWriterConfig config;
config.memory_mode = MemoryMode::HYBRID;
config.merge_policy = std::make_unique<TieredMergePolicy>();
config.hot_tier_ttl = std::chrono::days(7);

IndexWriter writer("/path/to/index", config);

// Add documents
Document doc;
doc.add_field(Field("title", "Lucene++ Design", FieldType::TEXT,
                    FieldStorage::INVERTED_INDEX));
doc.add_field(Field("price", 99.99, FieldType::DOUBLE,
                    FieldStorage::COLUMN_STORAGE));
doc.add_field(Field("timestamp", 1234567890, FieldType::INT64,
                    FieldStorage::BOTH));

writer.add_document(doc);
writer.commit();  // Flush to segment
```

### Index Searching

```cpp
// Open index reader
IndexReader reader("/path/to/index");

// Term query (inverted index)
TermQuery query("title", "lucene");
TopDocs results = reader.search(query, 10);

for (const auto& hit : results.hits) {
    Document doc = reader.document(hit.doc_id);
    std::cout << doc.get_field("title").as_string() << std::endl;
}

// Range query (column storage)
RangeQuery range_query("price", 50.0, 100.0);
TopDocs range_results = reader.search(range_query, 100);

// Aggregation (column scan)
AggregationQuery agg_query;
agg_query.add_metric("avg_price", AggregationType::AVG, "price");
agg_query.add_metric("max_price", AggregationType::MAX, "price");
AggregationResult agg = reader.aggregate(agg_query);
```

### Column Access (Analytical Queries)

```cpp
// Scan column for analytical processing
ColumnReader col_reader = reader.column("price");
ColumnIterator it = col_reader.iterator();

double sum = 0.0;
int count = 0;
while (it.has_next()) {
    double value = it.next().as_double();
    sum += value;
    count++;
}
double avg = sum / count;
```

## Query Execution Flow

### 1. Term Query (Inverted Index Path)

```
Query: title:lucene

1. IndexReader.search(query)
   ↓
2. Load term dictionary from all segments
   ↓
3. Term lookup: "lucene" → posting list offsets
   ↓
4. Load posting lists (with skip lists)
   ↓
5. Merge posting lists across segments (union)
   ↓
6. Score documents (TF-IDF or BM25)
   ↓
7. Return TopDocs (doc IDs + scores)
```

### 2. Range Query (Column Storage Path)

```
Query: price:[50 TO 100]

1. IndexReader.search(range_query)
   ↓
2. Load column storage for "price" field
   ↓
3. Check sparse index (granule-level min/max)
   ↓
4. Filter granules: skip if min > 100 or max < 50
   ↓
5. Scan selected granules
   ↓
6. Apply exact filter: 50 <= value <= 100
   ↓
7. Return matching doc IDs
```

### 3. Hybrid Query (Index + Column)

```
Query: title:lucene AND price:[50 TO 100]

1. Execute term query → Posting list A
2. Execute range query → Doc ID list B
3. Intersect A ∩ B (using skip lists)
4. Score combined results
5. Return TopDocs
```

## Circuit Breaker Integration Points

Although not implemented initially, the design includes hooks for circuit breakers:

```cpp
class CircuitBreaker {
public:
    virtual void check_memory_limit(size_t bytes) = 0;
    virtual void check_query_complexity(const Query& query) = 0;
    virtual void check_result_size(size_t num_hits) = 0;
    virtual void check_timeout(std::chrono::milliseconds elapsed) = 0;
};

// Integration points:
// - IndexWriter: Memory limit during indexing
// - IndexReader: Query complexity before execution
// - PostingListIterator: Result set size during merging
// - ColumnScanner: Scan timeout for large ranges
```

## Performance Considerations

### Memory Management
- **Arena allocators** for segment building (reduce fragmentation)
- **Memory pool** for query execution (reusable buffers)
- **mmap** for read-mostly data (OS manages paging)
- **Zero-copy** reads where possible

### Concurrency
- **Lock-free reads**: Immutable segments, atomic segment list updates
- **Write locks**: Single writer (Lucene model)
- **Merge threads**: Background merging without blocking writes
- **Thread-per-segment**: Parallel query execution

### Compression
- **Posting lists**: VByte encoding, bit packing, PForDelta
- **Columns**: Type-specific (LZ4 for fast decompression, ZSTD for high ratio)
- **Skip lists**: Compressed with delta encoding
- **Dictionary encoding**: For low-cardinality strings

## Future Extensions

1. **Distributed Index**: Sharding and replication (requires coordination)
2. **Vector Search**: Dense vector fields with HNSW/IVF indexes
3. **Sparse Vectors**: Integration with existing sparse ANN research
4. **Join Support**: Column-based joins for analytical queries
5. **Materialized Views**: Pre-aggregated columns for common queries
6. **Custom Codecs**: Plugin architecture for compression and encoding
7. **Query Cache**: Result caching for repeated queries
8. **Index Compaction**: More aggressive compression for cold tier

## Comparison with Related Systems

| Feature | Lucene++ | Apache Lucene | ClickHouse |
|---------|----------|---------------|------------|
| Language | C++ | Java | C++ |
| Inverted Index | ✓ | ✓ | ✗ |
| Column Storage | ✓ | ✗ | ✓ |
| Hybrid Queries | ✓ | Limited | Limited |
| Storage Tiers | ✓ | ✗ (via ILM) | Limited |
| Memory Mode | ✓ | ✗ | ✗ |
| Type Partitioning | ✓ | ✗ | ✓ |
| Trie Support | ✓ | ✓ (FST) | ✗ |

## References

- Apache Lucene: https://lucene.apache.org/
- ClickHouse Architecture: https://clickhouse.com/docs/en/development/architecture/
- OpenSearch Storage Tiers: https://opensearch.org/docs/latest/im-plugin/index-management/
- FST (Finite State Transducers): http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.24.3698
