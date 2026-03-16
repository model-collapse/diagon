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
 * SortedDocValuesWriter - Writes sorted (string) doc values to disk
 *
 * Format:
 *
 * .sdvm (metadata) file per segment:
 *   - Header: "DiagonSDV\0" (10 bytes magic)
 *   - version: int32 (1)
 *   - numFields: vInt
 *   - For each field:
 *     - fieldNumber (vInt)
 *     - fieldName (string)
 *     - numDocs (vInt) - total docs in segment
 *     - numValues (vInt) - docs with values
 *     - valueCount (vInt) - number of unique terms
 *     - dataOffset (vLong) - offset in .sdvd for ordinal column
 *     - dataLength (vLong) - length of ordinal column
 *     - dictOffset (vLong) - offset in .sdvd for term dictionary
 *     - dictLength (vLong) - length of term dictionary
 *   - Sentinel: fieldNumber = -1
 *
 * .sdvd (data) file per segment:
 *   - For each field:
 *     - Term dictionary: for each unique term (sorted):
 *       - termLength (vInt)
 *       - termBytes (byte[termLength])
 *     - Ordinal column: for each doc [0..numDocs):
 *       - ordinal (int32, fixed-width) — -1 if missing
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesConsumer
 */
class SortedDocValuesWriter {
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

    SortedDocValuesWriter(const std::string& segmentName, int maxDoc);

    void addValue(const index::FieldInfo& fieldInfo, int docID, const std::string& value);
    void addValue(const index::FieldInfo& fieldInfo, int docID, const uint8_t* bytes, int length);

    void finishField(const index::FieldInfo& fieldInfo);
    void flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut);
    int64_t ramBytesUsed() const;

private:
    struct FieldBuffer {
        std::string fieldName;
        int32_t fieldNumber{-1};
        std::vector<std::string> values;
        std::vector<bool> docsWithField;
        int32_t numValues{0};
        bool finished{false};
    };

    std::string segmentName_;
    int maxDoc_;
    std::unordered_map<int32_t, std::unique_ptr<FieldBuffer>> fieldBuffers_;
    std::vector<FieldMetadata> fieldMetadata_;

    FieldBuffer* getOrCreateBuffer(const index::FieldInfo& fieldInfo);
    void writeMetadata(store::IndexOutput& metaOut);
    FieldMetadata writeFieldData(store::IndexOutput& dataOut, const FieldBuffer& buffer);
};

}  // namespace codecs
}  // namespace diagon
