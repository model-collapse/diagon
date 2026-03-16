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
 * SortedSetDocValuesReader - Reads sorted-set doc values from disk
 *
 * Format (matches SortedSetDocValuesWriter):
 *
 * .ssdvm (metadata):
 *   - Header: "DiagonSSDV" magic, version
 *   - Per-field metadata with term dict and ordinal set offsets
 *   - Sentinel: fieldNumber = -1
 *
 * .ssdvd (data):
 *   - Term dictionary (sorted terms with length-prefixed bytes)
 *   - Per-doc ordinal sets (count + sorted ordinals)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesProducer (SortedSet)
 */
class SortedSetDocValuesReader {
public:
    struct FieldMetadata {
        std::string fieldName;
        int32_t fieldNumber{-1};
        int32_t numDocs{0};
        int32_t numValues{0};
        int64_t valueCount{0};
        int64_t totalOrdCount{0};
        int64_t dataOffset{0};
        int64_t dataLength{0};
        int64_t dictOffset{0};
        int64_t dictLength{0};
    };

    SortedSetDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                             std::unique_ptr<store::IndexInput> metaInput);

    std::unique_ptr<index::SortedSetDocValues> getSortedSet(const std::string& fieldName);
    std::unique_ptr<index::SortedSetDocValues> getSortedSet(int32_t fieldNumber);

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
        std::vector<std::vector<int64_t>> docOrdinals;
    };
    FieldData loadFieldData(const FieldMetadata& meta);
};

/**
 * In-memory implementation of SortedSetDocValues.
 * Iterates over documents, providing multi-valued ordinal access per doc.
 */
class MemorySortedSetDocValues : public index::SortedSetDocValues {
public:
    MemorySortedSetDocValues(std::vector<std::string> terms,
                             std::vector<std::vector<int64_t>> docOrdinals);

    // DocIdSetIterator
    int docID() const override { return docID_; }
    int nextDoc() override;
    int advance(int target) override;
    int64_t cost() const override { return cost_; }

    // DocValuesIterator
    bool advanceExact(int target) override;

    // SortedSetDocValues
    int64_t nextOrd() override;
    util::BytesRef lookupOrd(int64_t ord) const override;
    int64_t getValueCount() const override;

private:
    std::vector<std::string> terms_;
    std::vector<std::vector<int64_t>> docOrdinals_;
    int docID_{-1};
    int maxDoc_;
    int ordIndex_{0};
    int64_t cost_{0};
};

}  // namespace codecs
}  // namespace diagon
