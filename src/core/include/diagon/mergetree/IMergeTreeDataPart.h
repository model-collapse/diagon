// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace diagon {
namespace mergetree {

/**
 * Data part type (storage format)
 *
 * Based on: ClickHouse MergeTreeDataPartType
 */
enum class DataPartType {
    /**
     * Wide format: Each column in separate file
     * - field1/data.bin, field1/marks.mrk2
     * - field2/data.bin, field2/marks.mrk2
     * - primary.idx
     *
     * Used for large parts (> 10MB or > 100k rows)
     */
    Wide,

    /**
     * Compact format: All columns in single file
     * - data.bin (all columns interleaved by granule)
     * - marks.mrk3 (shared marks)
     *
     * Used for small parts (< 10MB or < 100k rows)
     */
    Compact,

    /**
     * InMemory format: Kept entirely in RAM
     * - Not persisted to disk
     * - Used for very small recent data
     *
     * NOTE: Not yet implemented
     */
    InMemory
};

/**
 * Data part state
 */
enum class DataPartState {
    /** Part is being written */
    Temporary,

    /** Part is complete and ready for queries */
    Active,

    /** Part is obsolete (replaced by merge) */
    Obsolete,

    /** Part deletion is in progress */
    Deleting
};

/**
 * IMergeTreeDataPart represents a single immutable data part.
 *
 * Based on: ClickHouse IMergeTreeDataPart
 *
 * NOTE: Stub implementation - provides interface only.
 * Full implementation requires:
 * - Column storage integration (Task #8)
 * - Granularity system (Task #11)
 * - Compression codecs (Task #9)
 * - Index structures
 */
class IMergeTreeDataPart {
public:
    virtual ~IMergeTreeDataPart() = default;

    // ==================== Type & Identity ====================

    /**
     * Get part type (Wide/Compact/InMemory)
     */
    virtual DataPartType getType() const = 0;

    /**
     * Get part name (e.g., "20240101_1_5_2")
     */
    virtual std::string getName() const = 0;

    /**
     * Get part state
     */
    virtual DataPartState getState() const = 0;

    // ==================== Size Information ====================

    /**
     * Number of rows in this part
     */
    virtual size_t getRowsCount() const = 0;

    /**
     * Bytes on disk
     */
    virtual size_t getBytesOnDisk() const = 0;

    /**
     * Number of marks (granules)
     */
    virtual size_t getMarksCount() const = 0;

    // ==================== Lifecycle ====================

    /**
     * Check if part is active (ready for queries)
     */
    bool isActive() const {
        return getState() == DataPartState::Active;
    }

    /**
     * Check if part is obsolete (replaced)
     */
    bool isObsolete() const {
        return getState() == DataPartState::Obsolete;
    }

    // ==================== Factory ====================

    /**
     * Select appropriate part type based on size
     */
    static DataPartType selectPartType(size_t bytes, size_t rows) {
        // Use Compact for small parts
        constexpr size_t MAX_COMPACT_BYTES = 10 * 1024 * 1024;  // 10MB
        constexpr size_t MAX_COMPACT_ROWS = 100'000;

        if (bytes < MAX_COMPACT_BYTES || rows < MAX_COMPACT_ROWS) {
            return DataPartType::Compact;
        }
        return DataPartType::Wide;
    }
};

using MergeTreeDataPartPtr = std::shared_ptr<IMergeTreeDataPart>;

/**
 * Convert DataPartType to string
 */
inline std::string dataPartTypeToString(DataPartType type) {
    switch (type) {
        case DataPartType::Wide:
            return "Wide";
        case DataPartType::Compact:
            return "Compact";
        case DataPartType::InMemory:
            return "InMemory";
    }
    return "Unknown";
}

/**
 * Convert string to DataPartType
 */
inline DataPartType stringToDataPartType(const std::string& str) {
    if (str == "Wide") return DataPartType::Wide;
    if (str == "Compact") return DataPartType::Compact;
    if (str == "InMemory") return DataPartType::InMemory;
    throw std::runtime_error("Unknown DataPartType: " + str);
}

}  // namespace mergetree
}  // namespace diagon
