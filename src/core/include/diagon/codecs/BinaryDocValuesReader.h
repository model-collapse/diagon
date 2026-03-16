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
 * BinaryDocValuesReader - Reads binary doc values from disk
 *
 * Format (matches BinaryDocValuesWriter):
 *
 * .bdvm (metadata) file per segment:
 *   - Header (codec name, version)
 *   - numFields (vInt)
 *   - For each field:
 *     - field number (vInt)
 *     - field name (string)
 *     - numDocs (vInt)
 *     - numValues (vInt)
 *     - offset in .bdvd file (vLong)
 *     - length in .bdvd file (vLong)
 *   - Sentinel: fieldNumber = -1
 *
 * .bdvd (data) file per segment:
 *   - For each field (at dataOffset):
 *     - For each doc: vInt(length) + byte[length]
 *     - hasValue bitmap: byte[(numDocs+7)/8]
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesProducer (binary variant)
 */
class BinaryDocValuesReader {
public:
    /**
     * Metadata for one binary field
     */
    struct FieldMetadata {
        std::string fieldName;
        int32_t fieldNumber{-1};
        int32_t numDocs{0};     // Total docs in segment
        int32_t numValues{0};   // Docs with values
        int64_t dataOffset{0};  // Offset in .bdvd file
        int64_t dataLength{0};  // Length in .bdvd file
    };

    /**
     * Constructor
     * @param dataInput Input stream for .bdvd file
     * @param metaInput Input stream for .bdvm file
     */
    BinaryDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                          std::unique_ptr<store::IndexInput> metaInput);

    /**
     * Get binary doc values for a field
     * @param fieldName Field name
     * @return BinaryDocValues instance, or nullptr if field not found
     */
    std::unique_ptr<index::BinaryDocValues> getBinary(const std::string& fieldName);

    /**
     * Get binary doc values by field number
     * @param fieldNumber Field number
     * @return BinaryDocValues instance, or nullptr if field not found
     */
    std::unique_ptr<index::BinaryDocValues> getBinary(int32_t fieldNumber);

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
     * Read metadata from .bdvm file
     */
    void readMetadata();

    /**
     * Loaded field data: per-doc values + hasValue bitmap
     */
    struct FieldData {
        std::vector<std::vector<uint8_t>> values;
        std::vector<bool> docsWithField;
    };

    /**
     * Load values for a field from .bdvd file
     */
    FieldData loadFieldData(const FieldMetadata& meta);
};

/**
 * Implementation of BinaryDocValues backed by in-memory arrays
 */
class MemoryBinaryDocValues : public index::BinaryDocValues {
public:
    /**
     * Constructor
     * @param values Per-doc binary values
     * @param docsWithField Bitmap of which docs have values
     */
    MemoryBinaryDocValues(std::vector<std::vector<uint8_t>> values,
                          std::vector<bool> docsWithField);

    // DocIdSetIterator interface
    int docID() const override { return docID_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return numValues_; }

    // DocValuesIterator interface
    bool advanceExact(int target) override;

    // BinaryDocValues interface
    util::BytesRef binaryValue() const override;

    // Reset iterator to initial state
    void reset() override { docID_ = -1; }

private:
    std::vector<std::vector<uint8_t>> values_;
    std::vector<bool> docsWithField_;
    int docID_{-1};
    int maxDoc_;
    int64_t numValues_;
};

}  // namespace codecs
}  // namespace diagon
