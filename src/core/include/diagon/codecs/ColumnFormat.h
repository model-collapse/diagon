// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace codecs {

// Forward declarations
class ColumnsConsumer;
class ColumnsProducer;
class SegmentWriteState;
class SegmentReadState;

/**
 * Data part type for column storage
 */
enum class DataPartType {
    WIDE,      // Separate file per column + marks
    COMPACT    // Single data.bin with shared marks
};

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
 *
 * NOTE: Stub implementation - full functionality requires:
 * - IColumn implementation (Task #8)
 * - Granularity system (Task #11)
 * - Compression codecs (Task #9)
 */
class ColumnFormat {
public:
    virtual ~ColumnFormat() = default;

    /**
     * Unique name
     */
    virtual std::string getName() const = 0;

    // ==================== Producer/Consumer ====================

    /**
     * Create consumer for writing columns
     *
     * NOTE: Stub - returns nullptr until column storage is implemented
     */
    virtual std::unique_ptr<ColumnsConsumer> fieldsConsumer(
        SegmentWriteState& state) = 0;

    /**
     * Create producer for reading columns
     *
     * NOTE: Stub - returns nullptr until column storage is implemented
     */
    virtual std::unique_ptr<ColumnsProducer> fieldsProducer(
        SegmentReadState& state) = 0;

    /**
     * Should use wide or compact format?
     * Based on segment size thresholds
     */
    virtual DataPartType selectPartType(int64_t estimatedBytes,
                                        int32_t estimatedDocs) const = 0;

    // ==================== Factory ====================

    static ColumnFormat& forName(const std::string& name);
    static void registerFormat(const std::string& name,
                               std::function<std::unique_ptr<ColumnFormat>()> factory);

private:
    static std::unordered_map<std::string, std::function<std::unique_ptr<ColumnFormat>()>>& getRegistry();
};

/**
 * Write columns
 *
 * NOTE: Stub interface
 */
class ColumnsConsumer {
public:
    virtual ~ColumnsConsumer() = default;
    virtual void close() = 0;

    // TODO: Add addColumn, writeColumn methods when IColumn is implemented
};

/**
 * Read columns
 *
 * NOTE: Stub interface
 */
class ColumnsProducer {
public:
    virtual ~ColumnsProducer() = default;
    virtual void checkIntegrity() = 0;
    virtual void close() = 0;

    // TODO: Add getColumn, getColumnRange methods when IColumn is implemented
};

}  // namespace codecs
}  // namespace diagon
