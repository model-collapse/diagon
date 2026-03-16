// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * SortedSetDocValuesWriter - Writes sorted-set doc values to disk
 *
 * Supports multiple deduplicated string values per document.
 *
 * Format:
 *
 * .ssdvm (metadata) file per segment:
 *   - Header: "DiagonSSDV" magic, version
 *   - For each field:
 *     - fieldNumber (vInt)
 *     - fieldName (string)
 *     - numDocs (vInt)
 *     - numValues (vInt) - docs with at least one value
 *     - valueCount (vLong) - unique terms in dictionary
 *     - totalOrdCount (vLong) - total ordinals across all docs
 *     - dataOffset (vLong) - offset in .ssdvd for per-doc ordinal data
 *     - dataLength (vLong)
 *     - dictOffset (vLong) - offset in .ssdvd for term dictionary
 *     - dictLength (vLong)
 *   - Sentinel: fieldNumber = -1
 *
 * .ssdvd (data) file per segment:
 *   - For each field:
 *     - Term dictionary: sorted terms with length-prefixed bytes
 *     - Per-doc ordinal sets: count + sorted ordinals per doc
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesConsumer (SortedSet)
 */
class SortedSetDocValuesWriter {
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

    SortedSetDocValuesWriter(const std::string& segmentName, int maxDoc);

    /**
     * Add a string value to a document's value set.
     * Can be called multiple times per doc. Duplicates within a doc are deduplicated.
     */
    void addValue(const index::FieldInfo& fieldInfo, int docID, const std::string& value);
    void addValue(const index::FieldInfo& fieldInfo, int docID, const uint8_t* bytes, int length);

    void finishField(const index::FieldInfo& fieldInfo);
    void flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut);
    int64_t ramBytesUsed() const;

private:
    struct FieldBuffer {
        std::string fieldName;
        int32_t fieldNumber{-1};
        std::vector<std::set<std::string>> values;  // Per-doc value sets (auto-dedup + sort)
        int32_t numValues{0};                       // Docs with at least one value
        bool finished{false};
    };

    std::string segmentName_;
    int maxDoc_;
    std::unordered_map<int32_t, std::unique_ptr<FieldBuffer>> fieldBuffers_;
    std::vector<FieldMetadata> fieldMetadata_;

    FieldBuffer* getOrCreateBuffer(const index::FieldInfo& fieldInfo);
    void writeMetadata(store::IndexOutput& metaOut);
    FieldMetadata writeFieldData(store::IndexOutput& dataOut, FieldBuffer& buffer);
};

}  // namespace codecs
}  // namespace diagon
