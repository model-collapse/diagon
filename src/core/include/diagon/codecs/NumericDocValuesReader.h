// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/DocValues.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * NumericDocValuesReader - Reads numeric doc values from disk
 *
 * Format (matches NumericDocValuesWriter):
 *
 * .dvm (metadata) file per segment:
 *   - Header (codec name, version)
 *   - For each field:
 *     - field number (vInt)
 *     - field name (string)
 *     - numDocs (vInt)
 *     - numValues (vInt)
 *     - offset in .dvd file (vLong)
 *     - length in .dvd file (vLong)
 *     - minValue (long)
 *     - maxValue (long)
 *
 * .dvd (data) file per segment:
 *   - For each field:
 *     - dense array of int64_t values (docID order)
 *     - missing docs encoded as 0
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesProducer
 */
class NumericDocValuesReader {
public:
    /**
     * Metadata for one numeric field
     */
    struct FieldMetadata {
        std::string fieldName;
        int32_t fieldNumber{-1};
        int32_t numDocs{0};      // Total docs in segment
        int32_t numValues{0};    // Docs with values
        int64_t dataOffset{0};   // Offset in .dvd file
        int64_t dataLength{0};   // Length in .dvd file
        int64_t minValue{0};
        int64_t maxValue{0};
    };

    /**
     * Constructor
     * @param dataInput Input stream for .dvd file
     * @param metaInput Input stream for .dvm file
     */
    NumericDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                           std::unique_ptr<store::IndexInput> metaInput);

    /**
     * Get numeric doc values for a field
     * @param fieldName Field name
     * @return NumericDocValues instance, or nullptr if field not found
     */
    std::unique_ptr<index::NumericDocValues> getNumeric(const std::string& fieldName);

    /**
     * Get numeric doc values by field number
     * @param fieldNumber Field number
     * @return NumericDocValues instance, or nullptr if field not found
     */
    std::unique_ptr<index::NumericDocValues> getNumeric(int32_t fieldNumber);

    /**
     * Check if field exists
     */
    bool hasField(const std::string& fieldName) const;

    /**
     * Check if field exists by number
     */
    bool hasField(int32_t fieldNumber) const;

    /**
     * Get field metadata
     */
    const FieldMetadata* getFieldMetadata(const std::string& fieldName) const;

private:
    // Input streams
    std::unique_ptr<store::IndexInput> dataInput_;
    std::unique_ptr<store::IndexInput> metaInput_;

    // Field metadata indexed by field name
    std::unordered_map<std::string, FieldMetadata> fieldsByName_;

    // Field metadata indexed by field number
    std::unordered_map<int32_t, FieldMetadata> fieldsByNumber_;

    /**
     * Read metadata from .dvm file
     */
    void readMetadata();

    /**
     * Load values for a field from .dvd file
     */
    std::vector<int64_t> loadValues(const FieldMetadata& meta);
};

/**
 * Implementation of NumericDocValues backed by in-memory array
 */
class MemoryNumericDocValues : public index::NumericDocValues {
public:
    /**
     * Constructor
     * @param values Dense array of values (one per doc)
     */
    explicit MemoryNumericDocValues(std::vector<int64_t> values);

    // DocIdSetIterator interface
    int docID() const override { return docID_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return static_cast<int64_t>(values_.size()); }

    // DocValuesIterator interface
    bool advanceExact(int target) override;

    // NumericDocValues interface
    int64_t longValue() const override;

    // Reset iterator to initial state (docID = -1)
    // Call this before reusing a cached iterator
    void reset() override {
        docID_ = -1;
    }

private:
    std::vector<int64_t> values_;
    int docID_{-1};
    int maxDoc_;
};

}  // namespace codecs
}  // namespace diagon
