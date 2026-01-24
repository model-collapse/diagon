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
class DocValuesConsumer;
class DocValuesProducer;
class SegmentWriteState;
class SegmentReadState;

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
 *
 * NOTE: Stub implementation - full functionality requires:
 * - Numeric encoding (Task TBD)
 * - Binary encoding (Task TBD)
 * - Sorted set encoding (Task TBD)
 */
class DocValuesFormat {
public:
    virtual ~DocValuesFormat() = default;

    /**
     * Unique name
     */
    virtual std::string getName() const = 0;

    // ==================== Producer/Consumer ====================

    /**
     * Create consumer for writing doc values
     *
     * NOTE: Stub - returns nullptr until formats are implemented
     */
    virtual std::unique_ptr<DocValuesConsumer> fieldsConsumer(
        SegmentWriteState& state) = 0;

    /**
     * Create producer for reading doc values
     *
     * NOTE: Stub - returns nullptr until formats are implemented
     */
    virtual std::unique_ptr<DocValuesProducer> fieldsProducer(
        SegmentReadState& state) = 0;

    // ==================== Factory ====================

    static DocValuesFormat& forName(const std::string& name);
    static void registerFormat(const std::string& name,
                               std::function<std::unique_ptr<DocValuesFormat>()> factory);

private:
    static std::unordered_map<std::string, std::function<std::unique_ptr<DocValuesFormat>()>>& getRegistry();
};

/**
 * Write doc values
 *
 * NOTE: Stub interface
 */
class DocValuesConsumer {
public:
    virtual ~DocValuesConsumer() = default;
    virtual void close() = 0;

    // TODO: Add addNumericField, addBinaryField, etc. when FieldInfo is connected
};

/**
 * Read doc values
 *
 * NOTE: Stub interface
 */
class DocValuesProducer {
public:
    virtual ~DocValuesProducer() = default;
    virtual void checkIntegrity() = 0;
    virtual void close() = 0;

    // TODO: Add getNumeric, getBinary, etc. when DocValues iterators are implemented
};

}  // namespace codecs
}  // namespace diagon
