// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/DocValues.h"
#include "diagon/index/FieldInfo.h"

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
     */
    virtual std::unique_ptr<DocValuesConsumer> fieldsConsumer(SegmentWriteState& state) = 0;

    /**
     * Create producer for reading doc values
     */
    virtual std::unique_ptr<DocValuesProducer> fieldsProducer(SegmentReadState& state) = 0;

    // ==================== Factory ====================

    static DocValuesFormat& forName(const std::string& name);
    static void registerFormat(const std::string& name,
                               std::function<std::unique_ptr<DocValuesFormat>()> factory);

private:
    static std::unordered_map<std::string, std::function<std::unique_ptr<DocValuesFormat>()>>&
    getRegistry();
};

/**
 * Write doc values to disk.
 *
 * Based on: org.apache.lucene.codecs.DocValuesConsumer
 *
 * The producer passed to add*Field methods provides the values
 * to be encoded. The consumer encodes them and writes to disk.
 */
class DocValuesConsumer {
public:
    virtual ~DocValuesConsumer() = default;

    /**
     * Write a numeric field.
     *
     * @param field Field metadata
     * @param valuesProducer Producer of numeric values to write
     */
    virtual void addNumericField(const index::FieldInfo& field,
                                 DocValuesProducer& valuesProducer) = 0;

    /**
     * Write a binary field.
     *
     * @param field Field metadata
     * @param valuesProducer Producer of binary values to write
     */
    virtual void addBinaryField(const index::FieldInfo& field,
                                DocValuesProducer& valuesProducer) = 0;

    /**
     * Write a sorted field (deferred to Phase 3).
     *
     * @param field Field metadata
     * @param valuesProducer Producer of sorted values to write
     */
    virtual void addSortedField(const index::FieldInfo& field,
                                DocValuesProducer& valuesProducer) = 0;

    /**
     * Write a sorted-set field (deferred to Phase 3).
     *
     * @param field Field metadata
     * @param valuesProducer Producer of sorted-set values to write
     */
    virtual void addSortedSetField(const index::FieldInfo& field,
                                   DocValuesProducer& valuesProducer) = 0;

    /**
     * Write a sorted-numeric field (deferred to Phase 3).
     *
     * @param field Field metadata
     * @param valuesProducer Producer of sorted-numeric values to write
     */
    virtual void addSortedNumericField(const index::FieldInfo& field,
                                       DocValuesProducer& valuesProducer) = 0;

    /**
     * Close and flush any pending data.
     */
    virtual void close() = 0;
};

/**
 * Read doc values from disk.
 *
 * Based on: org.apache.lucene.codecs.DocValuesProducer
 *
 * Provides iterators for accessing per-document values.
 */
class DocValuesProducer {
public:
    virtual ~DocValuesProducer() = default;

    /**
     * Get numeric doc values for a field.
     *
     * @param field Field metadata
     * @return Iterator over numeric values
     */
    virtual std::unique_ptr<index::NumericDocValues> getNumeric(const index::FieldInfo& field) = 0;

    /**
     * Get binary doc values for a field.
     *
     * @param field Field metadata
     * @return Iterator over binary values
     */
    virtual std::unique_ptr<index::BinaryDocValues> getBinary(const index::FieldInfo& field) = 0;

    /**
     * Get sorted doc values for a field (deferred to Phase 3).
     *
     * @param field Field metadata
     * @return Iterator over sorted values
     */
    virtual std::unique_ptr<index::SortedDocValues> getSorted(const index::FieldInfo& field) = 0;

    /**
     * Get sorted-set doc values for a field (deferred to Phase 3).
     *
     * @param field Field metadata
     * @return Iterator over sorted-set values
     */
    virtual std::unique_ptr<index::SortedSetDocValues>
    getSortedSet(const index::FieldInfo& field) = 0;

    /**
     * Get sorted-numeric doc values for a field (deferred to Phase 3).
     *
     * @param field Field metadata
     * @return Iterator over sorted-numeric values
     */
    virtual std::unique_ptr<index::SortedNumericDocValues>
    getSortedNumeric(const index::FieldInfo& field) = 0;

    /**
     * Check integrity of all doc values data.
     */
    virtual void checkIntegrity() = 0;

    /**
     * Close and release resources.
     */
    virtual void close() = 0;
};

}  // namespace codecs
}  // namespace diagon
