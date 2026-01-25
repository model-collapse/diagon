// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace diagon {
namespace index {

/**
 * Controls what information is indexed for a field
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

    std::string name;    // Field name (unique)
    int32_t number{-1};  // Global field number (unique, >= 0)

    // ==================== Indexing Configuration ====================

    IndexOptions indexOptions{IndexOptions::NONE};  // Posting list detail level
    bool storeTermVector{false};                    // Store term vectors?
    bool omitNorms{false};                          // Omit length normalization?
    bool storePayloads{false};                      // Store position payloads?

    // ==================== Doc Values Configuration ====================

    DocValuesType docValuesType{DocValuesType::NONE};                         // Column storage type
    DocValuesSkipIndexType docValuesSkipIndex{DocValuesSkipIndexType::NONE};  // Skip index type
    int64_t dvGen{-1};  // Doc values generation (-1 if none)

    // ==================== Point Values (Spatial/Numeric) ====================

    int32_t pointDimensionCount{0};       // Number of dimensions (0 if none)
    int32_t pointIndexDimensionCount{0};  // Dimensions used for indexing
    int32_t pointNumBytes{0};             // Bytes per dimension

    // ==================== Special Field Roles ====================

    bool softDeletesField{false};  // Is this the soft-deletes marker field?
    bool isParentField{false};     // Is this the parent document field?

    // ==================== Codec Metadata ====================

    std::map<std::string, std::string> attributes;  // Codec-specific extensions

    // ==================== Validation ====================

    /**
     * Validate field configuration
     * Throws std::invalid_argument if inconsistent
     */
    void validate() const;

    // ==================== Utility Methods ====================

    /**
     * Does this field have postings?
     */
    bool hasPostings() const { return indexOptions != IndexOptions::NONE; }

    /**
     * Does this field have frequencies?
     */
    bool hasFreqs() const { return indexOptions >= IndexOptions::DOCS_AND_FREQS; }

    /**
     * Does this field have positions?
     */
    bool hasPositions() const { return indexOptions >= IndexOptions::DOCS_AND_FREQS_AND_POSITIONS; }

    /**
     * Does this field have offsets?
     */
    bool hasOffsets() const {
        return indexOptions == IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
    }

    /**
     * Does this field have norms?
     */
    bool hasNorms() const { return !omitNorms && hasPostings(); }

    /**
     * Does this field have doc values?
     */
    bool hasDocValues() const { return docValuesType != DocValuesType::NONE; }

    /**
     * Does this field have point values?
     */
    bool hasPointValues() const { return pointDimensionCount > 0; }

    /**
     * Get attribute value
     */
    std::optional<std::string> getAttribute(const std::string& key) const;

    /**
     * Set attribute (for building)
     */
    void putAttribute(const std::string& key, const std::string& value);
};

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
    explicit FieldInfos(std::vector<FieldInfo> infos);

    /**
     * Default constructor for empty FieldInfos
     */
    FieldInfos() = default;

    // Delete copy operations (byName_ contains pointers into byNumber_)
    FieldInfos(const FieldInfos&) = delete;
    FieldInfos& operator=(const FieldInfos&) = delete;

    // Allow move operations
    FieldInfos(FieldInfos&&) noexcept = default;
    FieldInfos& operator=(FieldInfos&&) noexcept = default;

    // ==================== Lookup ====================

    /**
     * Get field by name
     * Returns nullptr if not found
     */
    const FieldInfo* fieldInfo(const std::string& fieldName) const;

    /**
     * Get field by number
     * Returns nullptr if not found
     */
    const FieldInfo* fieldInfo(int32_t fieldNumber) const;

    /**
     * Number of fields
     */
    size_t size() const { return byNumber_.size(); }

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

    // ==================== Special Fields ====================

    /**
     * Name of soft-deletes field (empty if none)
     */
    const std::string& getSoftDeletesField() const { return softDeletesField_; }

    /**
     * Name of parent document field (empty if none)
     */
    const std::string& getParentField() const { return parentField_; }

private:
    // Storage
    std::vector<FieldInfo> byNumber_;                 // Indexed by field number
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

    // Special fields
    std::string softDeletesField_;
    std::string parentField_;

    void buildIndex();
    void computeAggregateFlags();
    void validateSpecialFields();
};

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
    int32_t getOrAdd(const std::string& fieldName);

    /**
     * Get field by name
     * Returns nullptr if not found
     */
    FieldInfo* getFieldInfo(const std::string& fieldName);

    /**
     * Update field index options
     * Can only upgrade, not downgrade
     */
    void updateIndexOptions(const std::string& fieldName, IndexOptions indexOptions);

    /**
     * Update field doc values type
     * Creates field if it doesn't exist
     */
    void updateDocValuesType(const std::string& fieldName, DocValuesType docValuesType);

    /**
     * Get field number (returns -1 if not found)
     */
    int32_t getFieldNumber(const std::string& fieldName) const;

    /**
     * Get field count
     */
    int32_t getFieldCount() const { return static_cast<int32_t>(byName_.size()); }

    /**
     * Reset for reuse
     */
    void reset();

    /**
     * Build final FieldInfos
     */
    std::unique_ptr<FieldInfos> finish();

private:
    std::map<std::string, FieldInfo> byName_;
    int32_t nextFieldNumber_{0};
};

}  // namespace index
}  // namespace diagon
