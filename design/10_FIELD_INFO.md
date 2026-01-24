# Field Info System Design
## Based on Lucene FieldInfo & FieldInfos

Source references:
- `org.apache.lucene.index.FieldInfo`
- `org.apache.lucene.index.FieldInfos`
- `org.apache.lucene.index.IndexOptions`
- `org.apache.lucene.index.DocValuesType`
- `org.apache.lucene.codecs.FieldInfosFormat`
- `org.apache.lucene.codecs.lucene94.Lucene94FieldInfosFormat`

## Overview

Field metadata system tracking per-field configuration across segments. Each field has:
- **Identity**: Name and number
- **Indexing**: How it's indexed (docs only, with frequencies, with positions, etc.)
- **Doc Values**: Column-oriented storage for aggregations and sorting
- **Point Values**: Multi-dimensional numeric/spatial indexing
- **Attributes**: Codec-specific metadata

**Note**: Vector search (dense vector similarity search) is **deferred to v2.0**. This includes HNSW/IVF indexes, approximate nearest neighbor search, and vector similarity functions. For MVP, focus is on text search and analytical queries.

FieldInfo is immutable per segment. Field configurations are validated at construction time.

## IndexOptions (Enum)

```cpp
/**
 * Controls what information is indexed for field
 *
 * Based on: org.apache.lucene.index.IndexOptions
 */
enum class IndexOptions : uint8_t {
    /**
     * Not indexed - field may have doc values or be stored only
     */
    NONE = 0,

    /**
     * Index docs only (no frequencies, positions, or offsets)
     * Term queries work, phrase queries throw exception
     * Scoring treats each term as appearing once per doc
     */
    DOCS = 1,

    /**
     * Index docs and term frequencies (no positions or offsets)
     * Enables BM25 scoring but phrase queries throw exception
     */
    DOCS_AND_FREQS = 2,

    /**
     * Index docs, frequencies, and positions (no offsets)
     * Enables phrase queries and proximity scoring
     * Most common option for full-text search
     */
    DOCS_AND_FREQS_AND_POSITIONS = 3,

    /**
     * Index docs, frequencies, positions, and character offsets
     * Enables highlighting with exact character positions
     */
    DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS = 4
};
```

## DocValuesType (Enum)

```cpp
/**
 * Type of doc values (column-oriented storage)
 *
 * Based on: org.apache.lucene.index.DocValuesType
 */
enum class DocValuesType : uint8_t {
    /**
     * No doc values
     */
    NONE = 0,

    /**
     * Single 64-bit long per document
     * Use for: counts, timestamps, IDs
     * Storage: ~8 bytes per doc (compressed)
     */
    NUMERIC = 1,

    /**
     * Arbitrary byte[] per document (up to ~32KB)
     * Use for: strings as UTF-8, binary data
     * Storage: variable, compressed
     */
    BINARY = 2,

    /**
     * Pre-sorted unique byte[] values with per-doc ordinals
     * Use for: sorted string fields (sorting, aggregations)
     * Storage: shared dictionary + ordinals
     * Constraint: Limited to ~32KB per unique value
     */
    SORTED = 3,

    /**
     * Multiple sorted numeric values per document
     * Use for: multi-valued numeric fields
     */
    SORTED_NUMERIC = 4,

    /**
     * Multiple sorted byte[] values per document (set)
     * Use for: multi-valued string fields (facets, tags)
     */
    SORTED_SET = 5
};
```

## DocValuesSkipIndexType (Enum)

```cpp
/**
 * Skip index type for doc values (range query optimization)
 *
 * Based on: org.apache.lucene.index.DocValuesSkipIndexType
 */
enum class DocValuesSkipIndexType : uint8_t {
    /**
     * No skip index
     */
    NONE = 0,

    /**
     * Min/max range tracking per block
     * Enables skipping blocks that don't overlap query range
     * Compatible with: NUMERIC, SORTED_NUMERIC, SORTED, SORTED_SET
     */
    RANGE = 1
};
```

## FieldInfo (Core Structure)

```cpp
/**
 * Per-field metadata
 *
 * Immutable once constructed for a segment.
 * Validated at construction time.
 *
 * Based on: org.apache.lucene.index.FieldInfo
 */
struct FieldInfo {
    // ==================== Basic Identity ====================

    std::string name;           // Field name (unique)
    int32_t number;             // Global field number (unique, >= 0)

    // ==================== Indexing Configuration ====================

    IndexOptions indexOptions;  // Posting list detail level
    bool storeTermVector;       // Store term vectors?
    bool omitNorms;             // Omit length normalization?
    bool storePayloads;         // Store position payloads?

    // ==================== Doc Values Configuration ====================

    DocValuesType docValuesType;            // Column storage type
    DocValuesSkipIndexType docValuesSkipIndex;  // Skip index type
    int64_t dvGen;                          // Doc values generation (-1 if none)

    // ==================== Point Values (Spatial/Numeric) ====================

    int32_t pointDimensionCount;        // Number of dimensions (0 if none)
    int32_t pointIndexDimensionCount;   // Dimensions used for indexing
    int32_t pointNumBytes;              // Bytes per dimension

    // Note: Vector Values support (dense vector similarity search) is deferred to v2.0

    // ==================== Special Field Roles ====================

    bool softDeletesField;      // Is this the soft-deletes marker field?
    bool isParentField;         // Is this the parent document field?

    // ==================== Codec Metadata ====================

    std::map<std::string, std::string> attributes;  // Codec-specific extensions

    // ==================== Validation ====================

    /**
     * Validate field configuration
     * Throws if inconsistent
     */
    void validate() const {
        // Name and number
        if (name.empty()) {
            throw std::invalid_argument("Field name cannot be empty");
        }
        if (number < 0) {
            throw std::invalid_argument("Field number must be >= 0");
        }

        // Index options constraints
        if (indexOptions == IndexOptions::NONE) {
            if (storeTermVector) {
                throw std::invalid_argument("Cannot store term vectors for non-indexed field");
            }
            if (storePayloads) {
                throw std::invalid_argument("Cannot store payloads for non-indexed field");
            }
            if (!omitNorms) {
                omitNorms = true;  // Automatically omit norms for NONE
            }
        }

        // Payloads require positions
        if (storePayloads && indexOptions < IndexOptions::DOCS_AND_FREQS_AND_POSITIONS) {
            throw std::invalid_argument("Payloads require at least DOCS_AND_FREQS_AND_POSITIONS");
        }

        // Doc values skip index compatibility
        if (docValuesSkipIndex != DocValuesSkipIndexType::NONE) {
            if (docValuesType == DocValuesType::NONE || docValuesType == DocValuesType::BINARY) {
                throw std::invalid_argument("Skip index incompatible with NONE or BINARY doc values");
            }
        }

        // Point values consistency
        if (pointDimensionCount > 0) {
            if (pointIndexDimensionCount <= 0 || pointIndexDimensionCount > pointDimensionCount) {
                throw std::invalid_argument("Invalid pointIndexDimensionCount");
            }
            if (pointNumBytes <= 0) {
                throw std::invalid_argument("pointNumBytes must be > 0");
            }
        } else {
            if (pointIndexDimensionCount != 0 || pointNumBytes != 0) {
                throw std::invalid_argument("Point fields must be zero if pointDimensionCount=0");
            }
        }

        // Note: Vector values validation removed (deferred to v2.0)

        // Special field roles
        if (softDeletesField && isParentField) {
            throw std::invalid_argument("Field cannot be both soft-deletes and parent field");
        }
    }

    // ==================== Utility Methods ====================

    /**
     * Does this field have postings?
     */
    bool hasPostings() const {
        return indexOptions != IndexOptions::NONE;
    }

    /**
     * Does this field have frequencies?
     */
    bool hasFreqs() const {
        return indexOptions >= IndexOptions::DOCS_AND_FREQS;
    }

    /**
     * Does this field have positions?
     */
    bool hasPositions() const {
        return indexOptions >= IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    }

    /**
     * Does this field have offsets?
     */
    bool hasOffsets() const {
        return indexOptions == IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
    }

    /**
     * Does this field have norms?
     */
    bool hasNorms() const {
        return !omitNorms && hasPostings();
    }

    /**
     * Does this field have doc values?
     */
    bool hasDocValues() const {
        return docValuesType != DocValuesType::NONE;
    }

    /**
     * Does this field have point values?
     */
    bool hasPointValues() const {
        return pointDimensionCount > 0;
    }

    /**
     * Get attribute value
     */
    std::optional<std::string> getAttribute(const std::string& key) const {
        auto it = attributes.find(key);
        if (it != attributes.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * Set attribute (for building)
     */
    void putAttribute(const std::string& key, const std::string& value) {
        attributes[key] = value;
    }
};
```

## FieldInfos (Collection)

```cpp
/**
 * Collection of FieldInfo for a segment
 *
 * Provides fast lookup by name and number.
 * Tracks aggregate flags across all fields.
 *
 * Based on: org.apache.lucene.index.FieldInfos
 */
class FieldInfos {
public:
    // ==================== Construction ====================

    /**
     * Construct from vector of FieldInfo
     * Validates uniqueness and consistency
     */
    explicit FieldInfos(std::vector<FieldInfo> infos)
        : byNumber_(std::move(infos)) {

        buildIndex();
        computeAggregateFlags();
        validateSpecialFields();
    }

    // ==================== Lookup ====================

    /**
     * Get field by name
     * Returns nullptr if not found
     */
    const FieldInfo* fieldInfo(const std::string& fieldName) const {
        auto it = byName_.find(fieldName);
        return it != byName_.end() ? it->second : nullptr;
    }

    /**
     * Get field by number
     * Returns nullptr if not found
     */
    const FieldInfo* fieldInfo(int32_t fieldNumber) const {
        if (fieldNumber >= 0 && fieldNumber < static_cast<int32_t>(byNumber_.size())) {
            return &byNumber_[fieldNumber];
        }
        return nullptr;
    }

    /**
     * Number of fields
     */
    size_t size() const {
        return byNumber_.size();
    }

    // ==================== Iteration ====================

    /**
     * Iterator (in field number order)
     */
    auto begin() const { return byNumber_.begin(); }
    auto end() const { return byNumber_.end(); }

    // ==================== Aggregate Flags ====================

    bool hasFreq() const { return hasFreq_; }
    bool hasPostings() const { return hasPostings_; }
    bool hasProx() const { return hasProx_; }  // Has positions
    bool hasPayloads() const { return hasPayloads_; }
    bool hasOffsets() const { return hasOffsets_; }
    bool hasTermVectors() const { return hasTermVectors_; }
    bool hasNorms() const { return hasNorms_; }
    bool hasDocValues() const { return hasDocValues_; }
    bool hasPointValues() const { return hasPointValues_; }

    // Note: hasVectorValues() removed (vector search deferred to v2.0)

    // ==================== Special Fields ====================

    /**
     * Name of soft-deletes field (empty if none)
     */
    const std::string& getSoftDeletesField() const {
        return softDeletesField_;
    }

    /**
     * Name of parent document field (empty if none)
     */
    const std::string& getParentField() const {
        return parentField_;
    }

private:
    // Storage
    std::vector<FieldInfo> byNumber_;  // Indexed by field number
    std::map<std::string, const FieldInfo*> byName_;  // Name lookup

    // Aggregate flags
    bool hasFreq_{false};
    bool hasPostings_{false};
    bool hasProx_{false};
    bool hasPayloads_{false};
    bool hasOffsets_{false};
    bool hasTermVectors_{false};
    bool hasNorms_{false};
    bool hasDocValues_{false};
    bool hasPointValues_{false};

    // Note: hasVectorValues_ removed (vector search deferred to v2.0)

    // Special fields
    std::string softDeletesField_;
    std::string parentField_;

    void buildIndex() {
        for (const auto& info : byNumber_) {
            if (byName_.find(info.name) != byName_.end()) {
                throw std::invalid_argument("Duplicate field name: " + info.name);
            }
            byName_[info.name] = &info;
        }
    }

    void computeAggregateFlags() {
        for (const auto& info : byNumber_) {
            info.validate();  // Validate each field

            if (info.hasFreqs()) hasFreq_ = true;
            if (info.hasPostings()) hasPostings_ = true;
            if (info.hasPositions()) hasProx_ = true;
            if (info.storePayloads) hasPayloads_ = true;
            if (info.hasOffsets()) hasOffsets_ = true;
            if (info.storeTermVector) hasTermVectors_ = true;
            if (info.hasNorms()) hasNorms_ = true;
            if (info.hasDocValues()) hasDocValues_ = true;
            if (info.hasPointValues()) hasPointValues_ = true;
            // Note: hasVectorValues() check removed (vector search deferred to v2.0)
        }
    }

    void validateSpecialFields() {
        int softDeletesCount = 0;
        int parentFieldCount = 0;

        for (const auto& info : byNumber_) {
            if (info.softDeletesField) {
                softDeletesCount++;
                softDeletesField_ = info.name;
            }
            if (info.isParentField) {
                parentFieldCount++;
                parentField_ = info.name;
            }
        }

        if (softDeletesCount > 1) {
            throw std::invalid_argument("Multiple soft-deletes fields not allowed");
        }
        if (parentFieldCount > 1) {
            throw std::invalid_argument("Multiple parent fields not allowed");
        }
    }
};
```

## FieldInfosFormat (Codec Interface)

```cpp
/**
 * Codec for reading/writing FieldInfos
 *
 * Based on: org.apache.lucene.codecs.FieldInfosFormat
 */
class FieldInfosFormat {
public:
    virtual ~FieldInfosFormat() = default;

    /**
     * Read field infos from segment
     */
    virtual std::unique_ptr<FieldInfos> read(
        Directory& directory,
        const SegmentInfo& segmentInfo,
        const std::string& segmentSuffix,
        IOContext& ioContext) const = 0;

    /**
     * Write field infos to segment
     */
    virtual void write(
        Directory& directory,
        const SegmentInfo& segmentInfo,
        const std::string& segmentSuffix,
        const FieldInfos& infos,
        IOContext& context) const = 0;
};
```

## Lucene104FieldInfosFormat (Concrete Implementation)

```cpp
/**
 * Lucene 10.4 field infos format
 *
 * Based on: org.apache.lucene.codecs.lucene94.Lucene94FieldInfosFormat
 * (adapted for latest version)
 *
 * File extension: .fnm
 */
class Lucene104FieldInfosFormat : public FieldInfosFormat {
public:
    static constexpr const char* EXTENSION = "fnm";
    static constexpr const char* CODEC_NAME = "Lucene104FieldInfos";
    static constexpr int VERSION_START = 0;
    static constexpr int VERSION_CURRENT = 0;

    // ==================== Read ====================

    std::unique_ptr<FieldInfos> read(
        Directory& directory,
        const SegmentInfo& segmentInfo,
        const std::string& segmentSuffix,
        IOContext& ioContext) const override {

        std::string fileName = IndexFileNames::segmentFileName(
            segmentInfo.name, segmentSuffix, EXTENSION);

        auto input = directory.openInput(fileName, ioContext);

        // Read header
        CodecUtil::checkIndexHeader(*input, CODEC_NAME, VERSION_START, VERSION_CURRENT,
                                    segmentInfo.getId(), segmentSuffix);

        // Read field count
        int32_t size = input->readVInt();
        std::vector<FieldInfo> infos;
        infos.reserve(size);

        // Read each field
        for (int i = 0; i < size; ++i) {
            FieldInfo info = readFieldInfo(*input);
            infos.push_back(std::move(info));
        }

        // Read footer
        CodecUtil::checkFooter(*input);

        return std::make_unique<FieldInfos>(std::move(infos));
    }

    // ==================== Write ====================

    void write(
        Directory& directory,
        const SegmentInfo& segmentInfo,
        const std::string& segmentSuffix,
        const FieldInfos& infos,
        IOContext& context) const override {

        std::string fileName = IndexFileNames::segmentFileName(
            segmentInfo.name, segmentSuffix, EXTENSION);

        auto output = directory.createOutput(fileName, context);

        // Write header
        CodecUtil::writeIndexHeader(*output, CODEC_NAME, VERSION_CURRENT,
                                    segmentInfo.getId(), segmentSuffix);

        // Write field count
        output->writeVInt(infos.size());

        // Write each field
        for (const auto& info : infos) {
            writeFieldInfo(*output, info);
        }

        // Write footer
        CodecUtil::writeFooter(*output);
        output->close();
    }

private:
    // Field bits flags
    static constexpr uint8_t STORE_TERMVECTOR = 0x01;
    static constexpr uint8_t OMIT_NORMS = 0x02;
    static constexpr uint8_t STORE_PAYLOADS = 0x04;
    static constexpr uint8_t SOFT_DELETES_FIELD = 0x08;
    static constexpr uint8_t PARENT_FIELD = 0x10;

    FieldInfo readFieldInfo(IndexInput& input) const {
        FieldInfo info;

        // Read name and number
        info.name = input.readString();
        info.number = input.readVInt();

        // Read field bits
        uint8_t bits = input.readByte();
        info.storeTermVector = (bits & STORE_TERMVECTOR) != 0;
        info.omitNorms = (bits & OMIT_NORMS) != 0;
        info.storePayloads = (bits & STORE_PAYLOADS) != 0;
        info.softDeletesField = (bits & SOFT_DELETES_FIELD) != 0;
        info.isParentField = (bits & PARENT_FIELD) != 0;

        // Read index options
        info.indexOptions = static_cast<IndexOptions>(input.readByte());

        // Read doc values type
        info.docValuesType = static_cast<DocValuesType>(input.readByte());

        // Read doc values skip index
        info.docValuesSkipIndex = static_cast<DocValuesSkipIndexType>(input.readByte());

        // Read doc values generation
        info.dvGen = input.readLong();

        // Read attributes
        int32_t numAttributes = input.readVInt();
        for (int i = 0; i < numAttributes; ++i) {
            std::string key = input.readString();
            std::string value = input.readString();
            info.attributes[key] = value;
        }

        // Read point values
        info.pointDimensionCount = input.readVInt();
        if (info.pointDimensionCount > 0) {
            info.pointIndexDimensionCount = input.readVInt();
            info.pointNumBytes = input.readVInt();
        }

        // Note: Vector values reading removed (vector search deferred to v2.0)

        return info;
    }

    void writeFieldInfo(IndexOutput& output, const FieldInfo& info) const {
        // Write name and number
        output.writeString(info.name);
        output.writeVInt(info.number);

        // Write field bits
        uint8_t bits = 0;
        if (info.storeTermVector) bits |= STORE_TERMVECTOR;
        if (info.omitNorms) bits |= OMIT_NORMS;
        if (info.storePayloads) bits |= STORE_PAYLOADS;
        if (info.softDeletesField) bits |= SOFT_DELETES_FIELD;
        if (info.isParentField) bits |= PARENT_FIELD;
        output.writeByte(bits);

        // Write index options
        output.writeByte(static_cast<uint8_t>(info.indexOptions));

        // Write doc values type
        output.writeByte(static_cast<uint8_t>(info.docValuesType));

        // Write doc values skip index
        output.writeByte(static_cast<uint8_t>(info.docValuesSkipIndex));

        // Write doc values generation
        output.writeLong(info.dvGen);

        // Write attributes
        output.writeVInt(info.attributes.size());
        for (const auto& [key, value] : info.attributes) {
            output.writeString(key);
            output.writeString(value);
        }

        // Write point values
        output.writeVInt(info.pointDimensionCount);
        if (info.pointDimensionCount > 0) {
            output.writeVInt(info.pointIndexDimensionCount);
            output.writeVInt(info.pointNumBytes);
        }

        // Note: Vector values writing removed (vector search deferred to v2.0)
    }
};
```

## FieldInfosBuilder (Helper)

```cpp
/**
 * Builder for constructing FieldInfos during indexing
 *
 * Tracks global field numbers and ensures consistency
 */
class FieldInfosBuilder {
public:
    FieldInfosBuilder() = default;

    /**
     * Add or update field
     * Returns field number
     */
    int32_t getOrAdd(const std::string& fieldName) {
        auto it = byName_.find(fieldName);
        if (it != byName_.end()) {
            return it->second.number;
        }

        // Allocate new field number
        int32_t fieldNumber = nextFieldNumber_++;

        FieldInfo info;
        info.name = fieldName;
        info.number = fieldNumber;
        info.indexOptions = IndexOptions::NONE;
        info.docValuesType = DocValuesType::NONE;
        info.docValuesSkipIndex = DocValuesSkipIndexType::NONE;
        info.dvGen = -1;
        info.pointDimensionCount = 0;
        info.pointIndexDimensionCount = 0;
        info.pointNumBytes = 0;
        // Note: Vector field initialization removed (vector search deferred to v2.0)
        info.softDeletesField = false;
        info.isParentField = false;
        info.storeTermVector = false;
        info.omitNorms = false;
        info.storePayloads = false;

        byName_[fieldName] = info;
        return fieldNumber;
    }

    /**
     * Update field index options
     */
    void updateIndexOptions(const std::string& fieldName, IndexOptions indexOptions) {
        auto it = byName_.find(fieldName);
        if (it == byName_.end()) {
            throw std::invalid_argument("Field not found: " + fieldName);
        }

        FieldInfo& info = it->second;

        // Can only upgrade index options, not downgrade
        if (indexOptions > info.indexOptions) {
            info.indexOptions = indexOptions;
        }
    }

    /**
     * Build final FieldInfos
     */
    std::unique_ptr<FieldInfos> finish() {
        std::vector<FieldInfo> infos;
        infos.reserve(byName_.size());

        for (auto& [name, info] : byName_) {
            infos.push_back(std::move(info));
        }

        // Sort by field number
        std::sort(infos.begin(), infos.end(),
                  [](const FieldInfo& a, const FieldInfo& b) {
                      return a.number < b.number;
                  });

        return std::make_unique<FieldInfos>(std::move(infos));
    }

private:
    std::map<std::string, FieldInfo> byName_;
    int32_t nextFieldNumber_{0};
};
```

## Usage Example

```cpp
// Build field infos during indexing
FieldInfosBuilder builder;

// Add fields
int titleField = builder.getOrAdd("title");
builder.updateIndexOptions("title", IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);

int dateField = builder.getOrAdd("date");
FieldInfo* dateInfo = builder.getFieldInfo("date");
dateInfo->docValuesType = DocValuesType::NUMERIC;
dateInfo->indexOptions = IndexOptions::DOCS;

// Note: Vector field example removed (vector search deferred to v2.0)

// Build final field infos
auto fieldInfos = builder.finish();

// Write to segment
Lucene104FieldInfosFormat format;
format.write(*dir, segmentInfo, "", *fieldInfos, IOContext::DEFAULT);

// Read from segment
auto readInfos = format.read(*dir, segmentInfo, "", IOContext::DEFAULT);

// Lookup
const FieldInfo* titleInfo = readInfos->fieldInfo("title");
if (titleInfo && titleInfo->hasPositions()) {
    std::cout << "Title field has positions" << std::endl;
}

// Note: hasVectorValues() removed (vector search deferred to v2.0)
```

## Design Notes

### Immutability
- FieldInfo is immutable per segment
- Updates (like doc values generation) require new FieldInfo version
- Field configurations cannot change within a segment

### Validation
- All constraints enforced at construction time
- Codec writes only valid FieldInfo
- Reader validates on load

### Field Number Allocation
- Field numbers are global across segments
- Once assigned, never reused
- Enables consistent field identification

### Special Fields
- Soft-deletes field: Marks deleted documents without removing
- Parent field: Links child documents to parent in nested documents
- At most one of each per index

### Performance
- O(1) lookup by name (map) or number (vector)
- Aggregate flags avoid repeated traversal
- Compact binary format (~20-50 bytes per field)

---

**Design Status**: Complete ✅
**Next Module**: 11_SKIP_INDEXES.md (ClickHouse skip indexes)
