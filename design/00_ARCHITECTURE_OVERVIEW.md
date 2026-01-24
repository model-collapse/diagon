# Lucene++ Architecture Overview
## Based on Apache Lucene & ClickHouse Codebases

This design is derived from studying the actual implementations:
- **Lucene**: `/home/ubuntu/opensearch_warmroom/lucene/` (Java implementation)
- **ClickHouse**: `/home/ubuntu/opensearch_warmroom/ClickHouse/` (C++ implementation)

## Design Philosophy

**From Lucene:**
- Sealed interface hierarchies with clear inheritance (IndexReader → LeafReader/CompositeReader)
- Producer/Consumer pattern for codecs
- Pluggable codec architecture via factory registration
- Segment immutability with background merging
- Lock-free concurrent reads, single writer

**From ClickHouse:**
- Wide vs Compact part formats based on size thresholds
- Granule-based organization with adaptive granularity
- Type-specific serialization and compression
- Mark-based random access within compressed blocks
- Column-oriented storage with primary index

## Module Structure

```
lucenepp/
├── index/              # Indexing (Lucene-inspired)
│   ├── IndexWriter
│   ├── IndexReader (abstract)
│   ├── LeafReader (abstract)
│   ├── CompositeReader (abstract)
│   ├── DirectoryReader
│   ├── SegmentReader
│   ├── Terms/TermsEnum
│   ├── PostingsEnum
│   └── FieldInfos
├── store/              # Storage abstraction (Lucene-inspired)
│   ├── Directory (abstract)
│   ├── FSDirectory
│   ├── MMapDirectory
│   ├── IndexInput/IndexOutput
│   └── Lock
├── codecs/             # Codec architecture (Lucene-inspired)
│   ├── Codec (abstract)
│   ├── PostingsFormat
│   ├── DocValuesFormat
│   ├── ColumnFormat       # NEW: For column storage
│   ├── StoredFieldsFormat
│   └── Lucene104Codec     # Default implementation
├── column/             # Column storage (ClickHouse-inspired)
│   ├── IColumn (abstract)
│   ├── ColumnVector<T>
│   ├── ColumnString
│   ├── ColumnArray
│   ├── ColumnNullable
│   ├── IDataType (abstract)
│   ├── ISerialization
│   ├── MergeTreeDataPart  # Wide/Compact formats
│   ├── MergeTreeIndexGranularity
│   └── MarkInCompressedFile
├── search/             # Query execution (Lucene-inspired)
│   ├── Query (abstract)
│   ├── Weight (abstract)
│   ├── Scorer (abstract)
│   ├── IndexSearcher
│   ├── Collector
│   └── TopDocs
├── compression/        # Compression (ClickHouse-inspired)
│   ├── ICompressionCodec
│   ├── CompressionCodecLZ4
│   ├── CompressionCodecZSTD
│   ├── CompressionCodecDelta
│   ├── CompressionCodecGorilla
│   └── CompressionFactory
└── util/               # Utilities
    ├── BytesRef
    ├── BitSet
    └── NumericUtils
```

## Core Design Principles

### 1. Lucene-Style Sealed Hierarchies

```cpp
// Sealed reader hierarchy (C++20 style)
class IndexReader {
public:
    virtual ~IndexReader() = default;

    // Factory methods
    static std::unique_ptr<IndexReader> open(Directory& dir);
    static std::unique_ptr<IndexReader> openIfChanged(IndexReader& old);

    // Access sub-readers
    virtual std::vector<LeafReaderContext> leaves() const = 0;

    // Statistics
    virtual int numDocs() const = 0;
    virtual int maxDoc() const = 0;

    // Caching support
    virtual IndexReader::CacheHelper* getReaderCacheHelper() const = 0;
};

// Leaf reader for atomic segment
class LeafReader : public IndexReader {
public:
    // Terms access
    virtual Terms* terms(const std::string& field) const = 0;

    // Postings
    virtual PostingsEnum* postings(const Term& term, int flags) const = 0;

    // Doc values (column access)
    virtual NumericDocValues* getNumericDocValues(const std::string& field) const = 0;
    virtual BinaryDocValues* getBinaryDocValues(const std::string& field) const = 0;
    virtual SortedDocValues* getSortedDocValues(const std::string& field) const = 0;

    // Caching
    virtual LeafReader::CacheHelper* getCoreCacheHelper() const = 0;
};

// Composite reader wrapping multiple leaves
class CompositeReader : public IndexReader {
public:
    virtual std::vector<std::shared_ptr<IndexReader>> getSequentialSubReaders() const = 0;
};
```

### 2. ClickHouse-Style Column Storage

```cpp
// Data part types (ClickHouse MergeTreeDataPartType)
enum class DataPartType {
    Wide,      // Separate file per column + marks
    Compact,   // Single data.bin with shared marks
    InMemory   // Fully in memory
};

// ClickHouse mark structure
struct MarkInCompressedFile {
    uint64_t offset_in_compressed_file;      // File position
    uint64_t offset_in_decompressed_block;   // Block position
};

// Index granularity (ClickHouse MergeTreeIndexGranularity)
class IMergeTreeIndexGranularity {
public:
    virtual ~IMergeTreeIndexGranularity() = default;

    virtual size_t getMarksCount() const = 0;
    virtual size_t getMarkRows(size_t mark_index) const = 0;
    virtual size_t getRowsCountInRange(size_t begin, size_t end) const = 0;

    // Constant vs adaptive
    virtual bool hasFinalMark() const = 0;
    virtual bool empty() const = 0;
};

class MergeTreeIndexGranularityConstant : public IMergeTreeIndexGranularity {
private:
    size_t granularity_{8192};  // Rows per mark
    size_t num_marks_;
};

class MergeTreeIndexGranularityAdaptive : public IMergeTreeIndexGranularity {
private:
    std::vector<size_t> marks_rows_partial_sums_;  // Cumulative row counts
};
```

### 3. Lucene-Style Codec Architecture

```cpp
// Main codec (Lucene Codec)
class Codec {
public:
    virtual ~Codec() = default;

    // Codec identification
    virtual std::string getName() const = 0;

    // Format accessors
    virtual PostingsFormat& postingsFormat() = 0;
    virtual DocValuesFormat& docValuesFormat() = 0;
    virtual ColumnFormat& columnFormat() = 0;          // NEW
    virtual StoredFieldsFormat& storedFieldsFormat() = 0;
    virtual FieldInfosFormat& fieldInfosFormat() = 0;
    virtual SegmentInfoFormat& segmentInfoFormat() = 0;
    virtual LiveDocsFormat& liveDocsFormat() = 0;

    // Codec registration (SPI-style)
    static void registerCodec(const std::string& name,
                             std::function<std::unique_ptr<Codec>()> factory);
    static Codec& forName(const std::string& name);
    static Codec& getDefault();  // Returns Lucene104Codec
};

// Producer/Consumer pattern for postings
class PostingsFormat {
public:
    virtual std::string getName() const = 0;

    virtual FieldsConsumer* fieldsConsumer(SegmentWriteState& state) = 0;
    virtual FieldsProducer* fieldsProducer(SegmentReadState& state) = 0;
};

class FieldsConsumer {
public:
    virtual ~FieldsConsumer() = default;
    virtual void write(Fields& fields, NormsProducer* norms) = 0;
    virtual void close() = 0;
};

class FieldsProducer : public Fields {
public:
    virtual void checkIntegrity() = 0;
    virtual FieldsProducer* getMergeInstance() = 0;
    virtual void close() = 0;
};
```

### 4. ClickHouse-Style Compression

```cpp
// Compression codec interface (ClickHouse ICompressionCodec)
class ICompressionCodec {
public:
    virtual ~ICompressionCodec() = default;

    virtual uint8_t getMethodByte() const = 0;

    virtual uint32_t compress(const char* source, uint32_t source_size,
                             char* dest) const = 0;

    virtual uint32_t decompress(const char* source, uint32_t source_size,
                               char* dest, uint32_t uncompressed_size) const = 0;

    virtual bool isCompression() const = 0;
    virtual bool isGenericCompression() const = 0;
    virtual std::string getCodecDesc() const = 0;
};

// Compression methods (ClickHouse CompressionMethodByte)
enum class CompressionMethodByte : uint8_t {
    NONE            = 0x02,
    LZ4             = 0x82,
    ZSTD            = 0x90,
    Multiple        = 0x91,  // Codec chaining
    Delta           = 0x92,
    T64             = 0x93,
    DoubleDelta     = 0x94,
    Gorilla         = 0x95,
    FPC             = 0x98
};

// Compressed block format (16-byte header)
struct CompressedBlockHeader {
    uint8_t checksum[16];    // CityHash128
    uint8_t method;          // CompressionMethodByte
    uint32_t compressed_size;
    uint32_t uncompressed_size;
};
```

## Data Part Structure (Hybrid Design)

### Wide Format (ClickHouse-inspired, for large segments)

```
segment_0/
├── _metadata.json                    # Segment metadata
├── _columns.txt                      # Column schema
├── _checksums.txt                    # File checksums
├── inverted/                         # Lucene-style inverted index
│   ├── _terms_0.trie                 # Term dictionary (field 0)
│   ├── _postings_0.bin               # Posting lists
│   ├── _postings_0.skip              # Skip data
│   └── ...
├── columns/                          # ClickHouse-style columns
│   ├── price.float64/
│   │   ├── data.bin                  # Compressed granules
│   │   ├── marks.mrk2                # Mark positions
│   │   └── primary.idx               # Sparse primary index
│   ├── title.string/
│   │   ├── data.bin
│   │   ├── dict.bin                  # Dictionary (if low-cardinality)
│   │   ├── marks.mrk2
│   │   └── primary.idx
│   └── tags.array.string/            # Array type
│       ├── data.bin                  # Flattened values
│       ├── offsets.bin               # Array boundaries
│       ├── marks.mrk2
│       └── primary.idx
├── docvalues/                        # Lucene-style doc values
│   ├── _dv_price.bin
│   └── _dv_category.bin
└── deleted.bm                        # Deleted docs bitmap
```

### Compact Format (ClickHouse-inspired, for small segments < 10MB)

```
segment_1/
├── _metadata.json
├── _columns.txt
├── _checksums.txt
├── inverted/
│   ├── _terms.trie
│   ├── _postings.bin
│   └── _postings.skip
├── columns/
│   ├── data.bin                      # All columns interleaved
│   └── marks.mrk3                    # Unified marks
└── deleted.bm
```

## Key Interface Alignments

### From Lucene

1. **IndexReader sealed hierarchy**: LeafReader (atomic), CompositeReader (multi-segment), DirectoryReader (concrete)
2. **Producer/Consumer codec pattern**: FieldsProducer/Consumer, DocValuesProducer/Consumer
3. **Terms/TermsEnum/PostingsEnum**: Iterator-based term and posting access
4. **Query/Weight/Scorer**: Three-level query execution
5. **Directory abstraction**: FSDirectory, MMapDirectory, with Lock support
6. **FieldInfo**: Complete field metadata (indexOptions, docValuesType, etc.)

### From ClickHouse

1. **IColumn hierarchy**: ColumnVector<T>, ColumnString, ColumnArray, ColumnNullable
2. **IDataType + ISerialization**: Type-aware serialization
3. **MergeTreeDataPart**: Wide vs Compact formats based on size
4. **MergeTreeIndexGranularity**: Constant/adaptive granularity
5. **MarkInCompressedFile**: Two-level addressing (compressed block + offset within)
6. **ICompressionCodec**: Pluggable compression with method byte
7. **Primary index as Columns**: Vector of column pointers for composite keys

## Storage Tier Integration

Following Lucene's segment-based approach + ClickHouse's part management:

```cpp
enum class StorageTier {
    Hot,    // In-memory or mmap on NVMe
    Warm,   // Disk-based on SSD
    Cold    // HDD or S3
};

// Segment with tier information
class SegmentCommitInfo {
    std::string name;
    StorageTier tier;
    int64_t generation;
    uint32_t maxDoc;
    uint32_t delCount;

    // Lucene-style
    std::unique_ptr<Codec> codec;

    // ClickHouse-style
    DataPartType partType;  // Wide or Compact
    std::shared_ptr<IMergeTreeIndexGranularity> indexGranularity;
};
```

## Query Execution Flow

### Inverted Index Path (Lucene-style)

```
1. Query.createWeight(IndexSearcher, ScoreMode, boost)
2. Weight.scorer(LeafReaderContext)
3. Scorer.iterator() → DocIdSetIterator
4. Iterate: docID(), score()
5. Collector.collect(doc, score)
```

### Column Scan Path (ClickHouse-style)

```
1. LeafReader.getNumericDocValues(field) → NumericDocValues
2. NumericDocValues wraps ColumnReader
3. ColumnReader uses marks to skip to granules
4. Primary index pruning: check min/max
5. Read selected granules only
6. Decompress blocks on-demand
```

## Implementation Phases

### Phase 1: Core Indexing (Lucene-aligned)
- [x] Directory abstraction (FSDirectory, MMapDirectory)
- [x] IndexWriter with IndexWriterConfig
- [x] FieldInfo and FieldInfos
- [x] Basic Codec interface
- [x] Segment flushing

### Phase 2: Core Reading (Lucene-aligned)
- [x] IndexReader hierarchy (LeafReader, CompositeReader, DirectoryReader)
- [x] Terms, TermsEnum interfaces
- [x] PostingsEnum with flags
- [x] SegmentReader implementation

### Phase 3: Postings Format (Lucene-aligned)
- [x] PostingsFormat abstract class
- [x] FieldsProducer/FieldsConsumer
- [x] BlockTreeTermsWriter (FST-based)
- [x] VByte and FOR compression

### Phase 4: Column Storage (ClickHouse-aligned)
- [x] IColumn hierarchy
- [x] IDataType + ISerialization
- [x] ColumnFormat codec
- [x] MergeTreeDataPart (Wide/Compact)
- [x] MergeTreeIndexGranularity
- [x] MarkInCompressedFile structure

### Phase 5: Compression (ClickHouse-aligned)
- [x] ICompressionCodec interface
- [x] CompressionFactory
- [x] LZ4, ZSTD, Delta, Gorilla codecs
- [x] Codec chaining (Multiple)
- [x] CompressedReadBuffer/WriteBuffer

### Phase 6: Query Execution (Lucene-aligned)
- [x] Query hierarchy
- [x] Weight abstract class
- [x] Scorer with getMaxScore, advanceShallow
- [x] IndexSearcher
- [x] TopScoreDocCollector

### Phase 7: Doc Values (Lucene-aligned)
- [x] DocValuesFormat
- [x] DocValuesProducer/Consumer
- [x] Numeric/Binary/Sorted/SortedSet/SortedNumeric types
- [x] Integration with column storage

### Phase 8: Merge & Lifecycle
- [x] MergePolicy (TieredMergePolicy)
- [x] MergeScheduler
- [x] SegmentMerger
- [x] Storage tier migration

## References

### Apache Lucene Source Code
- `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/`
- Key interfaces: IndexReader, IndexWriter, Codec, PostingsFormat, DocValuesFormat

### ClickHouse Source Code
- `/home/ubuntu/opensearch_warmroom/ClickHouse/src/`
- Key modules: Storages/MergeTree/, Columns/, Compression/, DataTypes/

### Papers & Documentation
- Lucene Index Format: https://lucene.apache.org/core/9_0_0/core/org/apache/lucene/codecs/lucene90/package-summary.html
- ClickHouse MergeTree: https://clickhouse.com/docs/en/engines/table-engines/mergetree-family/mergetree
