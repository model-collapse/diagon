// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/simd/ColumnWindow.h"

#include <cstdint>
#include <string>
#include <vector>

namespace diagon {
namespace simd {

/**
 * Data type for column values
 */
enum class DataType {
    INT32,
    INT64,
    FLOAT32,
    FLOAT64,
    BINARY
};

/**
 * Column metadata for unified storage
 */
struct ColumnMetadata {
    std::string name;
    ColumnDensity density;
    DataType valueType;

    // For sparse columns (posting lists)
    bool hasFrequencies{false};  // Store term frequencies?
    bool hasPositions{false};    // Store positions?
    bool hasPayloads{false};     // Store payloads?

    // For dense columns (doc values)
    bool hasNulls{false};        // Nullable column?

    // Statistics for query optimization
    int64_t totalDocs{0};
    int64_t nonZeroDocs{0};
    float avgValue{0.0f};
    float maxValue{0.0f};
};

/**
 * Unified format supporting both posting lists and doc values
 *
 * Replaces:
 * - PostingsFormat (sparse columns)
 * - DocValuesFormat (dense columns)
 * - ColumnFormat (ClickHouse columns)
 *
 * Based on: SINDI paper + ClickHouse unified storage
 *
 * NOTE: Stub implementation - actual file I/O and SIMD operations
 * would require full codec implementation.
 */
class UnifiedColumnFormat {
public:
    explicit UnifiedColumnFormat(size_t windowSize = 100000)
        : windowSize_(windowSize) {}

    // ==================== Configuration ====================

    /**
     * Get window size
     */
    size_t getWindowSize() const {
        return windowSize_;
    }

    /**
     * Set window size
     */
    void setWindowSize(size_t size) {
        windowSize_ = size;
    }

    // ==================== Write API (Stub) ====================

    /**
     * Begin writing column
     *
     * NOTE: Stub - actual implementation would write to IndexOutput
     */
    void beginColumn(const std::string& columnName, const ColumnMetadata& metadata) {
        // Stub: would initialize output stream and write metadata
    }

    /**
     * Write sparse window (posting list)
     *
     * NOTE: Stub - actual implementation would encode and compress
     */
    template<typename ValueType>
    void writeSparseWindow(const ColumnWindow<ValueType>& window) {
        // Stub: would write window header, delta-encoded indices, and values
    }

    /**
     * Write dense window (doc values)
     *
     * NOTE: Stub - actual implementation would write with SIMD alignment
     */
    template<typename ValueType>
    void writeDenseWindow(const ColumnWindow<ValueType>& window) {
        // Stub: would write window header and SIMD-aligned values
    }

    /**
     * End writing column
     */
    void endColumn() {
        // Stub: would finalize and flush output
    }

    // ==================== Read API (Stub) ====================

    /**
     * Read column metadata
     *
     * NOTE: Stub - actual implementation would parse from file
     */
    ColumnMetadata readMetadata(const std::string& columnName) const {
        // Stub: would read metadata from index
        return ColumnMetadata{};
    }

    /**
     * Read sparse window
     *
     * NOTE: Stub - actual implementation would decompress and decode
     */
    template<typename ValueType>
    ColumnWindow<ValueType> readSparseWindow(int windowId) const {
        // Stub: would read from index
        return ColumnWindow<ValueType>{};
    }

    /**
     * Read dense window
     *
     * NOTE: Stub - actual implementation would read SIMD-aligned data
     */
    template<typename ValueType>
    ColumnWindow<ValueType> readDenseWindow(int windowId) const {
        // Stub: would read from index
        return ColumnWindow<ValueType>{};
    }

private:
    size_t windowSize_;
};

}  // namespace simd
}  // namespace diagon
