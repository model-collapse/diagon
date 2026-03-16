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
 * SortedDocValuesReader - Reads sorted (string) doc values from disk
 *
 * Format (matches SortedDocValuesWriter):
 *
 * .sdvm (metadata):
 *   - Header: "DiagonSDV\0" (10 bytes magic)
 *   - version: int32
 *   - numFields: vInt
 *   - Per field: fieldNumber, fieldName, numDocs, numValues, valueCount,
 *                dataOffset, dataLength, dictOffset, dictLength
 *   - Sentinel: fieldNumber = -1
 *
 * .sdvd (data):
 *   - Per field: term dictionary (sorted terms), then ordinal column (int32 per doc)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesProducer
 */
class SortedDocValuesReader {
public:
    struct FieldMetadata {
        std::string fieldName;
        int32_t fieldNumber{-1};
        int32_t numDocs{0};
        int32_t numValues{0};
        int32_t valueCount{0};
        int64_t dataOffset{0};
        int64_t dataLength{0};
        int64_t dictOffset{0};
        int64_t dictLength{0};
    };

    SortedDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                          std::unique_ptr<store::IndexInput> metaInput);

    std::unique_ptr<index::SortedDocValues> getSorted(const std::string& fieldName);
    std::unique_ptr<index::SortedDocValues> getSorted(int32_t fieldNumber);
    bool hasField(const std::string& fieldName) const;
    bool hasField(int32_t fieldNumber) const;
    const FieldMetadata* getFieldMetadata(const std::string& fieldName) const;

private:
    std::unique_ptr<store::IndexInput> dataInput_;
    std::unique_ptr<store::IndexInput> metaInput_;
    std::unordered_map<std::string, FieldMetadata> fieldsByName_;
    std::unordered_map<int32_t, FieldMetadata> fieldsByNumber_;

    void readMetadata();

    struct FieldData {
        std::vector<std::string> terms;
        std::vector<int32_t> ordinals;
    };
    FieldData loadFieldData(const FieldMetadata& meta);
};

/**
 * In-memory implementation of SortedDocValues backed by term dictionary + ordinal array.
 */
class MemorySortedDocValues : public index::SortedDocValues {
public:
    MemorySortedDocValues(std::vector<std::string> terms, std::vector<int32_t> ordinals);

    // DocIdSetIterator
    int docID() const override { return docID_; }
    int nextDoc() override;
    int advance(int target) override;
    int64_t cost() const override;

    // DocValuesIterator
    bool advanceExact(int target) override;

    // SortedDocValues
    int ordValue() const override;
    util::BytesRef lookupOrd(int ord) const override;
    int getValueCount() const override;

private:
    std::vector<std::string> terms_;
    std::vector<int32_t> ordinals_;
    int docID_{-1};
    int maxDoc_;
};

}  // namespace codecs
}  // namespace diagon
