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
 * NumericDocValuesWriter - Writes numeric doc values to disk
 *
 * Format (simplified vs Lucene90 - no compression for now):
 *
 * .dvm (metadata) file per segment:
 *   - Header (codec name, version)
 *   - For each field:
 *     - field number (vInt)
 *     - field name (string)
 *     - numDocs (vInt) - number of docs with values
 *     - offset in .dvd file (vLong)
 *     - length in .dvd file (vLong)
 *
 * .dvd (data) file per segment:
 *   - For each field:
 *     - dense array of int64_t values (docID order)
 *     - missing docs encoded as 0 (for now)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesConsumer
 *
 * TODO: Add compression (delta, GCD, bitpacking) in future
 */
class NumericDocValuesWriter {
public:
    /**
     * Metadata for one numeric field
     */
    struct FieldMetadata {
        std::string fieldName;
        int32_t fieldNumber;
        int32_t numDocs;      // Total docs in segment
        int32_t numValues;    // Docs with values
        int64_t dataOffset;   // Offset in .dvd file
        int64_t dataLength;   // Length in .dvd file
        int64_t minValue{0};  // Min value (for future delta compression)
        int64_t maxValue{0};  // Max value (for future compression)
    };

    /**
     * Constructor
     * @param segmentName Segment name (e.g., "_0")
     * @param maxDoc Total number of docs in segment
     */
    NumericDocValuesWriter(const std::string& segmentName, int maxDoc);

    /**
     * Add a numeric value for a field
     * @param fieldInfo Field metadata
     * @param docID Document ID (0-based, must be sequential)
     * @param value The numeric value
     */
    void addValue(const index::FieldInfo& fieldInfo, int docID, int64_t value);

    /**
     * Finish writing a field (called after all docs processed)
     * @param fieldInfo Field metadata
     */
    void finishField(const index::FieldInfo& fieldInfo);

    /**
     * Flush all fields to disk
     * @param dataOut Output stream for .dvd file
     * @param metaOut Output stream for .dvm file
     */
    void flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut);

    /**
     * Get RAM bytes used
     */
    int64_t ramBytesUsed() const;

private:
    /**
     * Per-field buffer holding values
     */
    struct FieldBuffer {
        std::string fieldName;
        int32_t fieldNumber{-1};
        std::vector<int64_t> values;      // Dense array, one per doc
        std::vector<bool> docsWithField;  // Bitmap of which docs have values
        int32_t numValues{0};             // Count of docs with values
        int64_t minValue{0};
        int64_t maxValue{0};
        bool finished{false};
    };

    std::string segmentName_;
    int maxDoc_;

    // Field buffers indexed by field number
    std::unordered_map<int32_t, std::unique_ptr<FieldBuffer>> fieldBuffers_;

    // Metadata for each field (filled during flush)
    std::vector<FieldMetadata> fieldMetadata_;

    /**
     * Get or create buffer for a field
     */
    FieldBuffer* getOrCreateBuffer(const index::FieldInfo& fieldInfo);

    /**
     * Write metadata file (.dvm)
     */
    void writeMetadata(store::IndexOutput& metaOut);

    /**
     * Write data for one field to .dvd file
     * @return FieldMetadata with offset/length filled
     */
    FieldMetadata writeFieldData(store::IndexOutput& dataOut, const FieldBuffer& buffer);
};

}  // namespace codecs
}  // namespace diagon
