# Diagon Core Library

**Module**: Core indexing, search, and codec system
**Based on**: Apache Lucene
**Design References**: Modules 01, 02, 07, 07a, 08, 09, 10

## Overview

The core library implements the fundamental indexing and search infrastructure of Diagon, following Apache Lucene's architecture with adaptations for C++ and integration with columnar storage.

## Module Structure

### Store (Module 09)
**Directory abstraction layer for filesystem I/O**

- `Directory`: Abstract base class for filesystem operations
- `FSDirectory`: Standard filesystem implementation
- `MMapDirectory`: Memory-mapped file implementation
- `IndexInput/IndexOutput`: File I/O interfaces
- `Lock`: Write lock for concurrent access control

**Implementation Notes**:
- Use platform-specific APIs (mmap on Linux/macOS, MapViewOfFile on Windows)
- Support for checksums on all reads/writes
- IOContext hints for sequential vs random access

### Util
**Core utility classes**

- `BytesRef`: Immutable byte sequence (similar to Java's BytesRef)
- `BitSet`: Efficient bit array for document deletion tracking
- `NumericUtils`: Encoding/decoding utilities for numeric values

### Index (Module 01, 10)
**IndexReader/Writer hierarchy and field metadata**

#### Reader Hierarchy
- `IndexReader`: Abstract base with factory methods
- `LeafReader`: Access to single segment (atomic reader)
- `CompositeReader`: Multiple segments aggregated
- `DirectoryReader`: Concrete implementation over directory
- `SegmentReader`: Single segment reader

**Key Design Points**:
- Sealed hierarchy (no external subclassing)
- Immutable readers (thread-safe reads)
- Cache helpers for query caching
- Context objects for segment metadata

#### Writer
- `IndexWriter`: Main indexing API
- `IndexWriterConfig`: Configuration object
- `DocumentsWriter`: Buffering and DWPT management
- `DocumentsWriterPerThread`: Per-thread indexing
- `LiveDocs`: Track deleted documents

**Concurrency Model**:
- Multiple DWPT threads for parallel indexing
- Single writer guarantee via write lock
- Lock-free concurrent reads
- WAL (Write-Ahead Log) for crash recovery

#### Field Metadata
- `FieldInfo`: Per-field metadata (name, type, options)
- `FieldInfos`: Collection of FieldInfo for segment
- `IndexOptions`: Enumeration for indexing level
- `DocValuesType`: Enumeration for doc values type

### Codecs (Module 02)
**Pluggable format system**

- `Codec`: Main codec abstraction
- `PostingsFormat`: Inverted index encoding
- `DocValuesFormat`: Lucene doc values encoding
- `ColumnFormat`: ClickHouse-style column encoding (NEW)
- `FieldsProducer/Consumer`: Producer/Consumer pattern for read/write

**Producer/Consumer Pattern**:
```cpp
// Read path
FieldsProducer* fields = codec.postingsFormat().fieldsProducer(...);
Terms* terms = fields->terms("field");
TermsEnum* termsEnum = terms->iterator();

// Write path
FieldsConsumer* fields = codec.postingsFormat().fieldsConsumer(...);
fields->write(field, termsEnum);
```

**Default Implementation**: `Lucene104Codec`
- FST-based term dictionary
- VByte postings encoding
- Block compression for doc IDs

### Search (Module 07, 07a)
**Query execution framework**

#### Query Architecture
- `Query`: Abstract query class (reusable, cacheable)
- `Weight`: Per-IndexSearcher query representation
- `Scorer`: Per-segment scoring iterator

**Three-Level Design**:
1. `Query`: Reusable across multiple searches
2. `Weight`: Created per IndexSearcher (statistics, normalization)
3. `Scorer`: Created per segment (actual iteration)

#### Execution
- `IndexSearcher`: Main search API
- `Collector`: Result collection abstraction
- `TopScoreDocCollector`: Top-K results with heap
- `DocIdSetIterator`: Posting list iterator

#### Filters (Module 07a)
- `Filter`: Non-scoring query filter
- `RangeFilter`, `TermFilter`: Concrete implementations
- `FilterCache`: LRU cache for filter results
- `DocIdSet`: Compact document ID set representation

**Integration with Skip Indexes**: Filters can leverage skip indexes (Module 11) for granule-level pruning, achieving 90%+ data skipping for analytical queries.

### Merge (Module 08)
**Segment lifecycle management**

- `MergePolicy`: Determines when/which segments to merge
  - `TieredMergePolicy`: Size-tiered merging (default)
  - `LogByteSizeMergePolicy`: Log-structured merge
- `MergeScheduler`: Background merge execution
  - `ConcurrentMergeScheduler`: Thread pool for merges
- `OneMerge`: Single merge operation
- `MergeSpecification`: Merge plan

**Merge Triggers**:
1. Flush: After segment is written
2. Commit: On commit if needed
3. ForceMerge: Manual compaction

**Write Amplification**: See design document 01_INDEX_READER_WRITER.md for WAF analysis and tuning guidelines.

## Implementation Status

### Completed
- [ ] Directory abstraction
- [ ] Utilities (BytesRef, BitSet, NumericUtils)
- [ ] FieldInfo system
- [ ] IndexReader hierarchy interfaces
- [ ] IndexWriter interfaces
- [ ] Codec architecture interfaces

### In Progress
- [ ] Lucene104Codec implementation
- [ ] Query execution framework
- [ ] Merge system

### TODO
- [ ] WAL implementation
- [ ] DWPT concurrency
- [ ] LiveDocs implementation
- [ ] Crash recovery
- [ ] Phrase queries with position matching

## Dependencies

### Internal
- `diagon_columns`: Column storage system (Module 03)
- `diagon_compression`: Compression codecs (Module 04)

### External
- ZLIB: Checksum and compression
- ZSTD, LZ4: High-performance compression

## Build Notes

### Compiler Requirements
- C++20 support (concepts, ranges, modules)
- GCC 11+, Clang 14+, MSVC 2022+

### Platform-Specific
- **Linux**: Use mmap for MMapDirectory
- **Windows**: Use MapViewOfFile
- **macOS**: Use mmap with specific flags

## Testing

### Unit Tests
- `DirectoryTest`: Filesystem operations
- `IndexReaderTest`: Reader hierarchy
- `IndexWriterTest`: Writer operations with concurrency
- `CodecTest`: Codec registration and format selection

### Integration Tests
- End-to-end indexing and search
- Crash recovery scenarios
- Concurrent indexing stress tests

### Benchmarks
- Indexing throughput
- Query latency
- Merge performance

## Performance Considerations

### Memory Management
- Reader pooling to avoid reopening
- Buffer reuse in DWPT
- Arena allocator for short-lived objects

### Concurrency
- Lock-free reads via immutability
- DWPT parallel indexing
- Background merge threads

### I/O
- Sequential writes for segments
- mmap for index reading
- Checksums for integrity

## References

### Design Documents
- `design/01_INDEX_READER_WRITER.md`: IndexReader/Writer detailed design
- `design/02_CODEC_ARCHITECTURE.md`: Codec system design
- `design/07_QUERY_EXECUTION.md`: Query execution framework
- `design/08_MERGE_SYSTEM.md`: Merge policy and scheduler
- `design/09_DIRECTORY_ABSTRACTION.md`: Directory layer
- `design/10_FIELD_INFO.md`: Field metadata system

### Lucene Source Code
- `org.apache.lucene.index.IndexReader`
- `org.apache.lucene.index.IndexWriter`
- `org.apache.lucene.codecs.Codec`
- `org.apache.lucene.search.IndexSearcher`

## Implementation Guidelines

### Naming Conventions
- Follow Lucene naming (e.g., `maxDoc()`, not `max_doc()`)
- Use camelCase for methods (Lucene style)
- Use PascalCase for classes

### Error Handling
- Use exceptions for error conditions (follow Lucene model)
- Provide detailed error messages with context
- Use RAII for resource management

### Thread Safety
- Document thread-safety guarantees
- Immutable objects where possible
- Explicit synchronization where needed

### Documentation
- Doxygen comments for public APIs
- Reference Lucene equivalents where applicable
- Document deviations from Lucene design

---

**Last Updated**: 2026-01-24
**Status**: Initial structure created, implementation in progress
