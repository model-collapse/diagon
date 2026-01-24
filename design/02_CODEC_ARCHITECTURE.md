# Codec Architecture Design
## Based on Lucene Codec System

Source references:
- Lucene Codec: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/codecs/Codec.java`
- PostingsFormat: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/codecs/PostingsFormat.java`
- DocValuesFormat: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/codecs/DocValuesFormat.java`
- Lucene104Codec: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/codecs/lucene104/Lucene104Codec.java`

## Overview

The codec architecture provides pluggable encoding/decoding for all index data structures:
- **Postings**: Inverted index (term → doc IDs, frequencies, positions)
- **Doc Values**: Column storage (doc → value)
- **Stored Fields**: Original document storage
- **Term Vectors**: Per-doc term positions
- **Norms**: Normalization factors
- **Points**: Numeric/geo indexing structures
- **Vectors**: KNN vector storage
- **Columns**: ClickHouse-style column storage (NEW)

## Codec Base Class

```cpp
/**
 * Codec encapsulates format for all index structures.
 *
 * Abstract base class - implementations provide specific formats.
 * Registered via Codec::registerCodec() for SPI-style discovery.
 *
 * Based on: org.apache.lucene.codecs.Codec
 */
class Codec {
public:
    virtual ~Codec() = default;

    // ==================== Format Accessors ====================

    /**
     * PostingsFormat encodes term → doc mappings
     */
    virtual PostingsFormat& postingsFormat() = 0;

    /**
     * DocValuesFormat encodes doc → value mappings (Lucene doc values)
     */
    virtual DocValuesFormat& docValuesFormat() = 0;

    /**
     * ColumnFormat encodes ClickHouse-style column storage (NEW)
     * Wide/Compact formats, granule-based, with marks
     */
    virtual ColumnFormat& columnFormat() = 0;

    /**
     * StoredFieldsFormat encodes stored document fields
     */
    virtual StoredFieldsFormat& storedFieldsFormat() = 0;

    /**
     * TermVectorsFormat encodes per-document term vectors
     */
    virtual TermVectorsFormat& termVectorsFormat() = 0;

    /**
     * FieldInfosFormat encodes field metadata
     */
    virtual FieldInfosFormat& fieldInfosFormat() = 0;

    /**
     * SegmentInfoFormat encodes segment metadata
     */
    virtual SegmentInfoFormat& segmentInfoFormat() = 0;

    /**
     * NormsFormat encodes normalization values
     */
    virtual NormsFormat& normsFormat() = 0;

    /**
     * LiveDocsFormat encodes deleted documents
     */
    virtual LiveDocsFormat& liveDocsFormat() = 0;

    /**
     * PointsFormat encodes BKD tree for numeric/geo indexing
     */
    virtual PointsFormat& pointsFormat() = 0;

    /**
     * VectorFormat encodes KNN vectors (HNSW, etc.)
     */
    virtual VectorFormat& vectorFormat() = 0;

    // ==================== Identification ====================

    /**
     * Unique codec name (e.g., "Lucene104")
     */
    virtual std::string getName() const = 0;

    // ==================== Factory & Registration ====================

    /**
     * Get default codec (Lucene104Codec)
     */
    static Codec& getDefault();

    /**
     * Get codec by name
     * @throws std::runtime_error if not found
     */
    static Codec& forName(const std::string& name);

    /**
     * Get all available codec names
     */
    static std::vector<std::string> availableCodecs();

    /**
     * Register codec (SPI pattern)
     * Called at static initialization
     */
    static void registerCodec(const std::string& name,
                              std::function<std::unique_ptr<Codec>()> factory);

    // ==================== Utilities ====================

    /**
     * Check if codec supports concurrent access
     */
    virtual bool supportsConcurrentAccess() const {
        return false;
    }

private:
    // Registry implementation
    static std::unordered_map<std::string, std::function<std::unique_ptr<Codec>()>>& getRegistry();
};
```

## PostingsFormat (Inverted Index)

```cpp
/**
 * PostingsFormat encodes/decodes inverted index.
 *
 * Producer/Consumer pattern:
 * - FieldsConsumer: Write during indexing
 * - FieldsProducer: Read during searching
 *
 * File extensions: .tim, .tip, .doc, .pos, .pay
 *
 * Based on: org.apache.lucene.codecs.PostingsFormat
 */
class PostingsFormat {
public:
    virtual ~PostingsFormat() = default;

    /**
     * Unique name (e.g., "Lucene104")
     */
    virtual std::string getName() const = 0;

    // ==================== Producer/Consumer ====================

    /**
     * Create consumer for writing postings
     * Called during segment flush
     */
    virtual std::unique_ptr<FieldsConsumer> fieldsConsumer(
        SegmentWriteState& state) = 0;

    /**
     * Create producer for reading postings
     * Called when opening segment
     */
    virtual std::unique_ptr<FieldsProducer> fieldsProducer(
        SegmentReadState& state) = 0;

    // ==================== Factory & Registration ====================

    static PostingsFormat& forName(const std::string& name);
    static void registerFormat(const std::string& name,
                               std::function<std::unique_ptr<PostingsFormat>()> factory);
};

/**
 * Write-side API for postings
 */
class FieldsConsumer {
public:
    virtual ~FieldsConsumer() = default;

    /**
     * Write all fields
     * @param fields Iterator over fields and their terms
     * @param norms Norms producer (for skip data optimization)
     */
    virtual void write(Fields& fields, NormsProducer* norms) = 0;

    /**
     * Merge postings from multiple segments
     * More efficient than iterating individually
     */
    virtual void merge(MergeState& mergeState, NormsProducer* norms) {
        // Default: iterate and write
        // Subclasses can optimize
    }

    virtual void close() = 0;
};

/**
 * Read-side API for postings
 */
class FieldsProducer : public Fields {
public:
    virtual ~FieldsProducer() = default;

    /**
     * Check file integrity (checksums)
     */
    virtual void checkIntegrity() = 0;

    /**
     * Get merge instance (may return this or optimized version)
     */
    virtual FieldsProducer* getMergeInstance() {
        return this;
    }

    virtual void close() = 0;
};

/**
 * Iterate over fields
 */
class Fields {
public:
    virtual ~Fields() = default;

    /**
     * Get Terms for a field
     * @return nullptr if field doesn't exist
     */
    virtual Terms* terms(const std::string& field) const = 0;

    /**
     * Iterate over all fields
     */
    virtual Iterator<std::string> iterator() const = 0;

    /**
     * Number of fields (-1 if unknown)
     */
    virtual int size() const = 0;
};
```

## Terms, TermsEnum, PostingsEnum

```cpp
/**
 * Terms provides access to terms in a field.
 * Based on: org.apache.lucene.index.Terms
 */
class Terms {
public:
    virtual ~Terms() = default;

    /**
     * Create terms iterator
     */
    virtual TermsEnum* iterator() const = 0;

    /**
     * Get terms statistics
     */
    virtual int64_t size() const = 0;          // Unique term count (-1 if unknown)
    virtual int64_t getSumTotalTermFreq() const = 0;  // Sum of all term freqs
    virtual int64_t getSumDocFreq() const = 0;        // Sum of all doc freqs
    virtual int getDocCount() const = 0;              // Docs with this field

    /**
     * Check if positions/offsets are indexed
     */
    virtual bool hasFreqs() const = 0;
    virtual bool hasPositions() const = 0;
    virtual bool hasOffsets() const = 0;
    virtual bool hasPayloads() const = 0;

    /**
     * Get stats for this field
     */
    virtual BytesRef getMin() const = 0;  // Smallest term
    virtual BytesRef getMax() const = 0;  // Largest term
};

/**
 * Iterate over terms in a field.
 * Stateful: seekCeil/seekExact, then next().
 *
 * Based on: org.apache.lucene.index.TermsEnum
 */
class TermsEnum {
public:
    virtual ~TermsEnum() = default;

    // ==================== Seek Operations ====================

    /**
     * Seek to term >= target
     * @return SeekStatus indicating found/not found/end
     */
    virtual SeekStatus seekCeil(const BytesRef& target) = 0;

    /**
     * Seek to exact term
     * @return true if found
     */
    virtual bool seekExact(const BytesRef& target) = 0;

    /**
     * Seek by ordinal (if supported)
     */
    virtual void seekExact(int64_t ord) {
        throw UnsupportedOperationException("seekExact(ord) not supported");
    }

    // ==================== Iteration ====================

    /**
     * Move to next term
     * @return term bytes, or empty if at end
     */
    virtual BytesRef next() = 0;

    /**
     * Current term
     */
    virtual BytesRef term() const = 0;

    /**
     * Current term ordinal (-1 if not supported)
     */
    virtual int64_t ord() const {
        return -1;
    }

    // ==================== Statistics ====================

    /**
     * Documents containing current term
     */
    virtual int docFreq() const = 0;

    /**
     * Total term frequency across all docs
     */
    virtual int64_t totalTermFreq() const = 0;

    // ==================== Postings Access ====================

    /**
     * Get postings for current term
     * @param reuse Reusable enum (may be nullptr)
     * @param flags Combination of PostingsEnum flags
     * @return PostingsEnum
     */
    virtual PostingsEnum* postings(PostingsEnum* reuse, int flags = PostingsEnum::FREQS) = 0;

    // ==================== Attributes ====================

    /**
     * Get attributes (impacts, etc.)
     */
    virtual AttributeSource& attributes() = 0;
};

enum class SeekStatus {
    FOUND,       // Exact match
    NOT_FOUND,   // After seek position but before end
    END          // Past last term
};

/**
 * Iterate over postings (doc IDs, freqs, positions).
 *
 * Extends DocIdSetIterator with term-specific data.
 *
 * Based on: org.apache.lucene.index.PostingsEnum
 */
class PostingsEnum : public DocIdSetIterator {
public:
    // Flags for requesting data
    static constexpr int FREQS = 1 << 0;
    static constexpr int POSITIONS = 1 << 1;
    static constexpr int OFFSETS = 1 << 2;
    static constexpr int PAYLOADS = 1 << 3;
    static constexpr int ALL = FREQS | POSITIONS | OFFSETS | PAYLOADS;

    /**
     * Term frequency in current document
     */
    virtual int freq() const = 0;

    /**
     * Next position (call freq() times)
     * @return position
     */
    virtual int nextPosition() = 0;

    /**
     * Start offset of current position
     */
    virtual int startOffset() const = 0;

    /**
     * End offset of current position
     */
    virtual int endOffset() const = 0;

    /**
     * Payload at current position
     * @return payload bytes or empty
     */
    virtual BytesRef getPayload() const = 0;
};
```

## DocValuesFormat (Column Storage - Lucene Style)

```cpp
/**
 * DocValuesFormat encodes doc → value mappings.
 *
 * Types:
 * - Numeric: doc → int64
 * - Binary: doc → bytes
 * - Sorted: doc → ord → bytes (sorted set)
 * - SortedSet: doc → multiple ords
 * - SortedNumeric: doc → multiple int64s
 *
 * Based on: org.apache.lucene.codecs.DocValuesFormat
 */
class DocValuesFormat {
public:
    virtual ~DocValuesFormat() = default;

    virtual std::string getName() const = 0;

    /**
     * Create consumer for writing doc values
     */
    virtual std::unique_ptr<DocValuesConsumer> fieldsConsumer(
        SegmentWriteState& state) = 0;

    /**
     * Create producer for reading doc values
     */
    virtual std::unique_ptr<DocValuesProducer> fieldsProducer(
        SegmentReadState& state) = 0;

    static DocValuesFormat& forName(const std::string& name);
};

/**
 * Write doc values
 */
class DocValuesConsumer {
public:
    virtual ~DocValuesConsumer() = default;

    virtual void addNumericField(const FieldInfo& field,
                                 DocValuesProducer* values) = 0;

    virtual void addBinaryField(const FieldInfo& field,
                                DocValuesProducer* values) = 0;

    virtual void addSortedField(const FieldInfo& field,
                                DocValuesProducer* values) = 0;

    virtual void addSortedSetField(const FieldInfo& field,
                                   DocValuesProducer* values) = 0;

    virtual void addSortedNumericField(const FieldInfo& field,
                                       DocValuesProducer* values) = 0;

    virtual void close() = 0;
};

/**
 * Read doc values
 */
class DocValuesProducer {
public:
    virtual ~DocValuesProducer() = default;

    virtual NumericDocValues* getNumeric(const FieldInfo& field) = 0;
    virtual BinaryDocValues* getBinary(const FieldInfo& field) = 0;
    virtual SortedDocValues* getSorted(const FieldInfo& field) = 0;
    virtual SortedSetDocValues* getSortedSet(const FieldInfo& field) = 0;
    virtual SortedNumericDocValues* getSortedNumeric(const FieldInfo& field) = 0;

    virtual void checkIntegrity() = 0;
    virtual void close() = 0;
};
```

## ColumnFormat (ClickHouse-Style Column Storage - NEW)

```cpp
/**
 * ColumnFormat encodes ClickHouse-style column storage.
 *
 * Features:
 * - Wide format: separate file per column + marks
 * - Compact format: single data.bin with shared marks
 * - Granule-based (8192 rows default)
 * - Type-specific serialization (IDataType + ISerialization)
 * - Sparse primary index on granule boundaries
 * - Mark files for random access
 *
 * File extensions:
 * - Wide: field.type/data.bin, field.type/marks.mrk2, field.type/primary.idx
 * - Compact: data.bin, marks.mrk3
 *
 * NEW: Hybrid of Lucene codec pattern + ClickHouse storage
 */
class ColumnFormat {
public:
    virtual ~ColumnFormat() = default;

    virtual std::string getName() const = 0;

    /**
     * Create consumer for writing columns
     */
    virtual std::unique_ptr<ColumnsConsumer> fieldsConsumer(
        SegmentWriteState& state) = 0;

    /**
     * Create producer for reading columns
     */
    virtual std::unique_ptr<ColumnsProducer> fieldsProducer(
        SegmentReadState& state) = 0;

    /**
     * Should use wide or compact format?
     * Based on segment size thresholds
     */
    virtual DataPartType selectPartType(int64_t estimatedBytes,
                                        int32_t estimatedDocs) const = 0;

    static ColumnFormat& forName(const std::string& name);
};

/**
 * Write columns
 */
class ColumnsConsumer {
public:
    virtual ~ColumnsConsumer() = default;

    /**
     * Add column field
     * @param field Field metadata
     * @param dataType ClickHouse IDataType
     * @param serialization ISerialization for this type
     * @param granularity Granule settings
     */
    virtual void addColumn(const FieldInfo& field,
                          const IDataType& dataType,
                          ISerialization& serialization,
                          const GranularityConfig& granularity) = 0;

    /**
     * Write column data
     */
    virtual void writeColumn(const std::string& fieldName,
                            const IColumn& column,
                            size_t startRow,
                            size_t numRows) = 0;

    /**
     * Flush and finalize
     */
    virtual void close() = 0;
};

/**
 * Read columns
 */
class ColumnsProducer {
public:
    virtual ~ColumnsProducer() = default;

    /**
     * Get column reader
     */
    virtual std::unique_ptr<IColumn> getColumn(const FieldInfo& field) = 0;

    /**
     * Get column for range of rows (granule-aware)
     */
    virtual std::unique_ptr<IColumn> getColumnRange(const FieldInfo& field,
                                                     size_t startRow,
                                                     size_t numRows) = 0;

    /**
     * Get marks for column (for seek operations)
     */
    virtual const std::vector<MarkInCompressedFile>& getMarks(
        const FieldInfo& field) = 0;

    /**
     * Get granularity info
     */
    virtual const IMergeTreeIndexGranularity& getGranularity() const = 0;

    virtual void checkIntegrity() = 0;
    virtual void close() = 0;
};
```

## Segment Read/Write State

```cpp
/**
 * Shared state for writing a segment
 */
struct SegmentWriteState {
    Directory& directory;
    SegmentInfo& segmentInfo;
    FieldInfos& fieldInfos;
    BufferedUpdates* deletes;    // May be nullptr
    IOContext& context;
    std::string segmentSuffix;   // For multi-format support
};

/**
 * Shared state for reading a segment
 */
struct SegmentReadState {
    Directory& directory;
    SegmentInfo& segmentInfo;
    FieldInfos& fieldInfos;
    IOContext& context;
    std::string segmentSuffix;
};
```

## Default Implementation: Lucene104Codec

```cpp
/**
 * Default codec implementation (version 104)
 *
 * Based on: org.apache.lucene.codecs.lucene104.Lucene104Codec
 */
class Lucene104Codec : public Codec {
public:
    Lucene104Codec()
        : postingsFormat_(std::make_unique<Lucene104PostingsFormat>())
        , docValuesFormat_(std::make_unique<Lucene104DocValuesFormat>())
        , columnFormat_(std::make_unique<MergeTreeColumnFormat>())
        , storedFieldsFormat_(std::make_unique<Lucene104StoredFieldsFormat>())
        , termVectorsFormat_(std::make_unique<Lucene104TermVectorsFormat>())
        , fieldInfosFormat_(std::make_unique<Lucene104FieldInfosFormat>())
        , segmentInfoFormat_(std::make_unique<Lucene104SegmentInfoFormat>())
        , normsFormat_(std::make_unique<Lucene104NormsFormat>())
        , liveDocsFormat_(std::make_unique<Lucene104LiveDocsFormat>())
        , pointsFormat_(std::make_unique<Lucene104PointsFormat>())
        , vectorFormat_(std::make_unique<Lucene104VectorFormat>()) {}

    std::string getName() const override {
        return "Lucene104";
    }

    PostingsFormat& postingsFormat() override { return *postingsFormat_; }
    DocValuesFormat& docValuesFormat() override { return *docValuesFormat_; }
    ColumnFormat& columnFormat() override { return *columnFormat_; }
    StoredFieldsFormat& storedFieldsFormat() override { return *storedFieldsFormat_; }
    TermVectorsFormat& termVectorsFormat() override { return *termVectorsFormat_; }
    FieldInfosFormat& fieldInfosFormat() override { return *fieldInfosFormat_; }
    SegmentInfoFormat& segmentInfoFormat() override { return *segmentInfoFormat_; }
    NormsFormat& normsFormat() override { return *normsFormat_; }
    LiveDocsFormat& liveDocsFormat() override { return *liveDocsFormat_; }
    PointsFormat& pointsFormat() override { return *pointsFormat_; }
    VectorFormat& vectorFormat() override { return *vectorFormat_; }

private:
    std::unique_ptr<PostingsFormat> postingsFormat_;
    std::unique_ptr<DocValuesFormat> docValuesFormat_;
    std::unique_ptr<ColumnFormat> columnFormat_;
    std::unique_ptr<StoredFieldsFormat> storedFieldsFormat_;
    std::unique_ptr<TermVectorsFormat> termVectorsFormat_;
    std::unique_ptr<FieldInfosFormat> fieldInfosFormat_;
    std::unique_ptr<SegmentInfoFormat> segmentInfoFormat_;
    std::unique_ptr<NormsFormat> normsFormat_;
    std::unique_ptr<LiveDocsFormat> liveDocsFormat_;
    std::unique_ptr<PointsFormat> pointsFormat_;
    std::unique_ptr<VectorFormat> vectorFormat_;
};

// Register at startup
static struct Lucene104CodecRegistrar {
    Lucene104CodecRegistrar() {
        Codec::registerCodec("Lucene104", []() {
            return std::make_unique<Lucene104Codec>();
        });
    }
} g_lucene104CodecRegistrar;
```

## Usage Example

```cpp
// Use default codec
IndexWriterConfig config;
// config.setCodec() not called → uses Codec::getDefault() → Lucene104Codec

IndexWriter writer(dir, config);
writer.addDocument(doc);
writer.commit();

// Or use custom codec
auto customCodec = std::make_unique<CustomCodec>();
config.setCodec(std::move(customCodec));

// Or by name
config.setCodec(Codec::forName("Lucene104"));
```

## Key Design Decisions

1. **Producer/Consumer Pattern**: Separates read/write concerns
2. **Factory Registration**: SPI-style codec discovery
3. **Format Independence**: Each format can evolve independently
4. **Stateful Iterators**: TermsEnum, PostingsEnum for efficient traversal
5. **ColumnFormat Addition**: Bridges Lucene codec pattern with ClickHouse column storage
6. **Lazy Loading**: Formats create readers on-demand
7. **Merge Optimization**: Formats can override merge() for efficiency

---

## Format Evolution and Versioning

### Version Numbering Scheme

Codec versions follow Lucene's major version numbering:

```cpp
// Current codec
class Lucene104Codec : public Codec {
    std::string getName() const override { return "Lucene104"; }
    // Version 104 = Lucene 10.4.x compatible
};

// Future codec
class Lucene105Codec : public Codec {
    std::string getName() const override { return "Lucene105"; }
    // Version 105 = Lucene 10.5.x compatible
};
```

**Versioning Policy**:
- Major version bump (104 → 105) for any format change
- Version embedded in all file headers (magic bytes + version)
- Each segment records the codec that wrote it (in `segments_N` file)

### Backward Compatibility Guarantees

**Read Compatibility**: Lucene++ can read indexes from older codec versions.

```cpp
class SegmentReader {
    static std::unique_ptr<SegmentReader> open(
        Directory& dir,
        const SegmentCommitInfo& info,
        IOContext context
    ) {
        // Read codec name from segment metadata
        std::string codecName = info.info.getCodec();

        // Get codec by name (e.g., "Lucene104")
        std::unique_ptr<Codec> codec = Codec::forName(codecName);

        // Codec reads its own format
        return std::make_unique<SegmentReader>(dir, info, codec.get(), context);
    }
};
```

**Compatibility Matrix**:

| Writer Codec | Reader Codec | Supported? | Notes |
|--------------|--------------|------------|-------|
| Lucene104    | Lucene104    | ✅ Yes     | Exact match |
| Lucene104    | Lucene105    | ✅ Yes     | Newer reader reads older index |
| Lucene105    | Lucene104    | ❌ No      | Older reader cannot read newer format |
| Lucene90     | Lucene104    | ✅ Yes     | Multiple versions back supported |

**Guarantee**: Lucene++ version N can read all indexes from version N-1, N-2, ..., down to a minimum supported version (e.g., Lucene90).

### Forward Compatibility

**Forward compatibility is NOT guaranteed**: Older code cannot read indexes written by newer code.

**Rationale**:
- New features may require new file formats
- Optimizations may change on-disk structure
- Cannot predict future evolution

**Protection Mechanism**:
```cpp
class CodecUtil {
    static void checkHeader(
        DataInput& input,
        const std::string& codec,
        int minVersion,
        int maxVersion
    ) {
        int actualVersion = input.readInt();
        if (actualVersion < minVersion || actualVersion > maxVersion) {
            throw IOException(
                "Unsupported codec version: " + std::to_string(actualVersion) +
                " (expected: " + std::to_string(minVersion) +
                "-" + std::to_string(maxVersion) + ")"
            );
        }
    }
};
```

Each format reader checks version bounds and **rejects** unknown future versions.

### Migration Strategies

#### Strategy 1: Automatic Upgrade on Merge (Default)

**Approach**: Segments are rewritten with new codec during merge.

```cpp
class IndexWriter {
    void forceMerge(int maxSegments) {
        // Merges all segments
        // Output segments use current codec (from IndexWriterConfig)
        // Old segments gradually disappear as they're merged
    }
};
```

**Timeline**: Gradual upgrade over time as merges happen naturally.

**Pros**:
- No downtime
- No explicit reindex operation
- Segments upgrade incrementally

**Cons**:
- Mixed codec versions during transition
- Full upgrade may take days/weeks
- Merge overhead

**Example**:
```cpp
// Before: 10 segments written with Lucene104
// After upgrade to Lucene105:
//   - IndexWriter uses Lucene105 for new segments
//   - Old segments remain as Lucene104 (readable)
//   - Merge combines Lucene104 + Lucene104 → Lucene105
//   - Eventually all segments become Lucene105
```

#### Strategy 2: Explicit Reindex

**Approach**: Create new index, copy all documents.

```cpp
void reindexWithNewCodec(
    Directory& oldDir,
    Directory& newDir,
    std::unique_ptr<Codec> newCodec
) {
    // Open old index
    DirectoryReader reader = DirectoryReader::open(oldDir);

    // Create new writer with new codec
    IndexWriterConfig config;
    config.setCodec(std::move(newCodec));
    IndexWriter writer(newDir, config);

    // Copy all documents
    for (const LeafReaderContext& ctx : reader.leaves()) {
        LeafReader* leaf = ctx.reader();
        for (int doc = 0; doc < leaf->maxDoc(); ++doc) {
            if (!leaf->liveDocs() || leaf->liveDocs()->get(doc)) {
                Document d = leaf->document(doc);
                writer.addDocument(d);
            }
        }
    }

    writer.commit();
    writer.close();
}
```

**Timeline**: Immediate upgrade (limited by reindex speed).

**Pros**:
- Clean cutover
- All segments use new codec
- Opportunity to clean up deleted docs

**Cons**:
- Downtime or dual-index serving
- High CPU/IO cost
- Requires 2× storage during transition

#### Strategy 3: In-Place Upgrade (Rare)

Only possible if format changes are minor and backward-compatible.

**Example**: Adding optional metadata field that old readers can ignore.

**Not generally supported** - most format changes require rewriting.

### Feature Detection

Codecs can advertise capabilities to avoid runtime errors:

```cpp
/**
 * Codec capability flags
 */
enum class CodecCapability : uint64_t {
    POSTINGS            = 1 << 0,  // Supports inverted index
    DOC_VALUES          = 1 << 1,  // Supports doc values
    COLUMN_STORAGE      = 1 << 2,  // Supports ClickHouse columns
    SKIP_INDEXES        = 1 << 3,  // Supports skip indexes
    SIMD_ACCELERATION   = 1 << 4,  // Supports SIMD postings/columns
    VECTORS             = 1 << 5,  // Supports KNN vectors (future)
    COMPRESSION_ZSTD    = 1 << 6,  // Supports ZSTD compression
    ADAPTIVE_GRANULES   = 1 << 7,  // Supports adaptive granularity
};

class Codec {
public:
    /**
     * Query codec capabilities
     */
    virtual uint64_t getCapabilities() const = 0;

    bool hasCapability(CodecCapability cap) const {
        return (getCapabilities() & static_cast<uint64_t>(cap)) != 0;
    }
};

class Lucene104Codec : public Codec {
    uint64_t getCapabilities() const override {
        return static_cast<uint64_t>(CodecCapability::POSTINGS) |
               static_cast<uint64_t>(CodecCapability::DOC_VALUES) |
               static_cast<uint64_t>(CodecCapability::COLUMN_STORAGE) |
               static_cast<uint64_t>(CodecCapability::SKIP_INDEXES) |
               static_cast<uint64_t>(CodecCapability::SIMD_ACCELERATION) |
               static_cast<uint64_t>(CodecCapability::COMPRESSION_ZSTD) |
               static_cast<uint64_t>(CodecCapability::ADAPTIVE_GRANULES);
        // Note: VECTORS not included (deferred to v2.0)
    }
};
```

**Usage**:
```cpp
void IndexWriter::addDocument(const Document& doc) {
    Codec* codec = config_.getCodec();

    // Check if codec supports required features
    for (const IndexableField& field : doc.fields()) {
        if (field.hasColumnStorage() &&
            !codec->hasCapability(CodecCapability::COLUMN_STORAGE)) {
            throw UnsupportedOperationException(
                "Codec " + codec->getName() +
                " does not support column storage"
            );
        }
    }

    // Proceed with indexing
    // ...
}
```

### Deprecation Policy

**Policy**: Support N-2 versions backward, deprecate older versions.

**Example Timeline**:
```
Current: Lucene105 (active development)
├─ Lucene104 (supported, read/write)
├─ Lucene103 (supported, read-only)
├─ Lucene102 (deprecated, read-only, removal warning)
└─ Lucene101 (removed, not supported)
```

**Deprecation Process**:

1. **Phase 1: Announcement** (release N+1)
   - Announce deprecation of version N-2
   - Documentation updated with migration guidance
   - Warning logs when opening old indexes

2. **Phase 2: Read-Only** (release N+2)
   - Cannot create new segments with deprecated codec
   - Can still read existing segments
   - Strong warnings in logs

3. **Phase 3: Removal** (release N+3)
   - Codec code removed
   - Attempting to open old index throws exception
   - Users must reindex

**Example Code**:
```cpp
std::unique_ptr<Codec> Codec::forName(const std::string& name) {
    auto it = getCodecRegistry().find(name);
    if (it == getCodecRegistry().end()) {
        throw IOException("Unknown codec: " + name);
    }

    // Check deprecation status
    if (isDeprecated(name)) {
        LOG_WARN("Codec " + name + " is deprecated and will be removed in future version. "
                 "Please reindex with current codec: " + getDefault()->getName());
    }

    return it->second();
}
```

### Version Information

Each segment stores codec version information:

```cpp
class SegmentInfo {
    std::string codecName_;   // e.g., "Lucene104"

    // Written to .si file
    void write(DataOutput& output) const {
        // ... other fields ...
        output.writeString(codecName_);
    }

    static SegmentInfo read(DataInput& input) {
        // ... other fields ...
        std::string codecName = input.readString();
        // ...
    }
};
```

**File Header Format** (all codec files):
```
Byte 0-3:   Magic number (codec-specific, e.g., 0x3FD76C17 for postings)
Byte 4-7:   Format version (int32, e.g., 104)
Byte 8-15:  Object ID (unique per segment file)
Byte 16-19: Suffix version (format-specific minor version)
```

**Example**:
```cpp
class CodecUtil {
    static constexpr int32_t MAGIC_POSTINGS = 0x3FD76C17;

    static void writeHeader(
        DataOutput& output,
        const std::string& codec,
        int version
    ) {
        output.writeInt(MAGIC_POSTINGS);
        output.writeInt(version);  // e.g., 104
        output.writeLong(generateObjectID());
        output.writeInt(0);  // suffix version
    }
};
```

### Handling Format Changes

When introducing a new format, follow this checklist:

**For New Codec (e.g., Lucene105)**:

1. ✅ Create new codec class: `Lucene105Codec`
2. ✅ Increment version in file headers: 104 → 105
3. ✅ Update `SegmentInfo` to record new codec name
4. ✅ Implement backward-compatible readers for Lucene104
5. ✅ Add capability flags for new features
6. ✅ Update documentation with migration guide
7. ✅ Add deprecation warning for Lucene102
8. ✅ Register codec: `Codec::registerCodec("Lucene105", factory)`
9. ✅ Update default codec: `Codec::setDefault("Lucene105")`

**For Format-Specific Change** (e.g., new PostingsFormat):

1. ✅ Create new format class: `Lucene105PostingsFormat`
2. ✅ Increment format version in headers
3. ✅ Old codec still available (Lucene104PostingsFormat)
4. ✅ New codec references new format
5. ✅ Merge rewrites segments with new format

### Testing Compatibility

**Compatibility Test Suite**:
```cpp
TEST(CodecCompatibility, ReadLucene104WithLucene105) {
    // Write index with Lucene104
    Directory dir1 = createRAMDirectory();
    IndexWriterConfig config1;
    config1.setCodec(std::make_unique<Lucene104Codec>());
    IndexWriter writer1(dir1, config1);
    writer1.addDocument(createTestDocument());
    writer1.commit();
    writer1.close();

    // Read with Lucene105 (current)
    DirectoryReader reader = DirectoryReader::open(dir1);
    ASSERT_EQ(1, reader.numDocs());
    // Verify document contents
    Document doc = reader.document(0);
    ASSERT_EQ("test", doc.get("field"));
}

TEST(CodecCompatibility, RejectFutureVersion) {
    // Simulate future version (106)
    Directory dir = createRAMDirectory();
    IndexOutput out = dir.createOutput("test.dat", IOContext::DEFAULT);
    CodecUtil::writeHeader(out, "PostingsFormat", 106);  // Future version
    out.close();

    // Try to read with Lucene105
    IndexInput in = dir.openInput("test.dat", IOContext::DEFAULT);
    ASSERT_THROW(
        CodecUtil::checkHeader(in, "PostingsFormat", 104, 105),  // Max version 105
        IOException
    );
}
```

---
