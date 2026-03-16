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
 * SortedNumericDocValuesReader - Reads sorted-numeric doc values from disk
 *
 * Format (matches SortedNumericDocValuesWriter):
 *
 * .sndvm (metadata):
 *   - Header: "DiagonSNDV", version
 *   - numFields (vInt)
 *   - Per field: fieldNumber, fieldName, numDocs, numValues, totalValueCount,
 *                dataOffset, dataLength
 *   - Sentinel: fieldNumber = -1
 *
 * .sndvd (data):
 *   - Per field: counts array (vInt per doc) + values array (int64 each)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesProducer (SortedNumeric)
 */
class SortedNumericDocValuesReader {
public:
    struct FieldMetadata {
        std::string fieldName;
        int32_t fieldNumber{-1};
        int32_t numDocs{0};
        int32_t numValues{0};
        int64_t totalValueCount{0};
        int64_t dataOffset{0};
        int64_t dataLength{0};
    };

    SortedNumericDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                                 std::unique_ptr<store::IndexInput> metaInput);

    std::unique_ptr<index::SortedNumericDocValues> getSortedNumeric(const std::string& fieldName);
    std::unique_ptr<index::SortedNumericDocValues> getSortedNumeric(int32_t fieldNumber);

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
        std::vector<std::vector<int64_t>> values;
    };
    FieldData loadFieldData(const FieldMetadata& meta);
};

/**
 * In-memory implementation of SortedNumericDocValues.
 * Each doc has a sorted vector of int64 values.
 */
class MemorySortedNumericDocValues : public index::SortedNumericDocValues {
public:
    explicit MemorySortedNumericDocValues(std::vector<std::vector<int64_t>> values);

    // DocIdSetIterator
    int docID() const override { return docID_; }
    int nextDoc() override;
    int advance(int target) override;
    int64_t cost() const override { return cost_; }

    // DocValuesIterator
    bool advanceExact(int target) override;

    // SortedNumericDocValues
    int64_t nextValue() override;
    int docValueCount() const override;

    void reset() override {
        docID_ = -1;
        valueIndex_ = 0;
    }

private:
    std::vector<std::vector<int64_t>> values_;
    int docID_{-1};
    int maxDoc_;
    int valueIndex_{0};
    int64_t cost_;
};

}  // namespace codecs
}  // namespace diagon
