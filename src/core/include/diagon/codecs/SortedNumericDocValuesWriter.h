// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * SortedNumericDocValuesWriter - Writes sorted-numeric doc values to disk
 *
 * Supports multiple numeric values per document (sorted ascending within each doc).
 *
 * Format:
 *
 * .sndvm (metadata) file per segment:
 *   - Header: "DiagonSNDV" (magic), version (int32)
 *   - numFields (vInt)
 *   - For each field:
 *     - fieldNumber (vInt)
 *     - fieldName (string)
 *     - numDocs (vInt) - total docs in segment
 *     - numValues (vInt) - docs with at least one value
 *     - totalValueCount (vLong) - sum of all value counts
 *     - dataOffset (vLong)
 *     - dataLength (vLong)
 *   - Sentinel: fieldNumber = -1
 *
 * .sndvd (data) file per segment:
 *   - For each field (at dataOffset):
 *     - Counts array: vInt per doc (0 if no values)
 *     - Values array: int64 per value (sorted ascending within each doc)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesConsumer (SortedNumeric)
 */
class SortedNumericDocValuesWriter {
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

    SortedNumericDocValuesWriter(const std::string& segmentName, int maxDoc);

    /**
     * Add a single value to a doc's value set.
     * Can be called multiple times per doc to add multiple values.
     */
    void addValue(const index::FieldInfo& fieldInfo, int docID, int64_t value);

    void finishField(const index::FieldInfo& fieldInfo);

    void flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut);

    int64_t ramBytesUsed() const;

private:
    struct FieldBuffer {
        std::string fieldName;
        int32_t fieldNumber{-1};
        std::vector<std::vector<int64_t>> values;
        int32_t numValues{0};
        int64_t totalValueCount{0};
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
