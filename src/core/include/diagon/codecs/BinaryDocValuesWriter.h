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
 * BinaryDocValuesWriter - Writes binary doc values to disk
 *
 * Format:
 *
 * .bdvm (metadata) file per segment:
 *   - Header (codec name, version)
 *   - numFields (vInt)
 *   - For each field:
 *     - field number (vInt)
 *     - field name (string)
 *     - numDocs (vInt) - total docs in segment
 *     - numValues (vInt) - docs with values
 *     - offset in .bdvd file (vLong)
 *     - length in .bdvd file (vLong)
 *   - Sentinel: fieldNumber = -1
 *
 * .bdvd (data) file per segment:
 *   - For each field (at dataOffset):
 *     - For each doc [0..numDocs):
 *       - length (vInt) + bytes (byte[length])
 *     - hasValue bitmap: byte[(numDocs+7)/8], bit i = doc i has value
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90DocValuesConsumer (binary variant)
 */
class BinaryDocValuesWriter {
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
     * @param segmentName Segment name (e.g., "_0")
     * @param maxDoc Total number of docs in segment
     */
    BinaryDocValuesWriter(const std::string& segmentName, int maxDoc);

    /**
     * Add a binary value for a field
     * @param fieldInfo Field metadata
     * @param docID Document ID (0-based)
     * @param bytes Pointer to binary data
     * @param length Length of binary data
     */
    void addValue(const index::FieldInfo& fieldInfo, int docID, const uint8_t* bytes, int length);

    /**
     * Add a binary value from a string
     * @param fieldInfo Field metadata
     * @param docID Document ID (0-based)
     * @param value String value (stored as raw bytes)
     */
    void addValue(const index::FieldInfo& fieldInfo, int docID, const std::string& value);

    /**
     * Finish writing a field (called after all docs processed)
     * @param fieldInfo Field metadata
     */
    void finishField(const index::FieldInfo& fieldInfo);

    /**
     * Flush all fields to disk
     * @param dataOut Output stream for .bdvd file
     * @param metaOut Output stream for .bdvm file
     */
    void flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut);

    /**
     * Get RAM bytes used
     */
    int64_t ramBytesUsed() const;

private:
    /**
     * Per-field buffer holding binary values
     */
    struct FieldBuffer {
        std::string fieldName;
        int32_t fieldNumber{-1};
        std::vector<std::vector<uint8_t>> values;  // One per doc (empty vec = missing)
        std::vector<bool> docsWithField;           // Bitmap of which docs have values
        int32_t numValues{0};                      // Count of docs with values
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
     * Write metadata file (.bdvm)
     */
    void writeMetadata(store::IndexOutput& metaOut);

    /**
     * Write data for one field to .bdvd file
     * @return FieldMetadata with offset/length filled
     */
    FieldMetadata writeFieldData(store::IndexOutput& dataOut, const FieldBuffer& buffer);
};

}  // namespace codecs
}  // namespace diagon
