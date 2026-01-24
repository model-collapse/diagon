// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

// Forward declarations
class PostingsFormat;
class DocValuesFormat;
class ColumnFormat;
class StoredFieldsFormat;
class TermVectorsFormat;
class FieldInfosFormat;
class SegmentInfoFormat;
class NormsFormat;
class LiveDocsFormat;
class PointsFormat;
class VectorFormat;

/**
 * Codec encapsulates format for all index structures.
 *
 * Abstract base class - implementations provide specific formats.
 * Registered via Codec::registerCodec() for SPI-style discovery.
 *
 * Based on: org.apache.lucene.codecs.Codec
 *
 * NOTE: This is the base infrastructure. Format implementations are stubs
 * and will be completed in subsequent tasks.
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

    /**
     * Codec capability flags
     */
    enum class Capability : uint64_t {
        POSTINGS            = 1 << 0,  // Supports inverted index
        DOC_VALUES          = 1 << 1,  // Supports doc values
        COLUMN_STORAGE      = 1 << 2,  // Supports ClickHouse columns
        SKIP_INDEXES        = 1 << 3,  // Supports skip indexes
        SIMD_ACCELERATION   = 1 << 4,  // Supports SIMD postings/columns
        VECTORS             = 1 << 5,  // Supports KNN vectors
        COMPRESSION_ZSTD    = 1 << 6,  // Supports ZSTD compression
        ADAPTIVE_GRANULES   = 1 << 7,  // Supports adaptive granularity
    };

    /**
     * Query codec capabilities
     */
    virtual uint64_t getCapabilities() const = 0;

    bool hasCapability(Capability cap) const {
        return (getCapabilities() & static_cast<uint64_t>(cap)) != 0;
    }

private:
    // Registry implementation
    static std::unordered_map<std::string, std::function<std::unique_ptr<Codec>()>>& getRegistry();
    static std::string& getDefaultCodecName();
};

}  // namespace codecs
}  // namespace diagon
