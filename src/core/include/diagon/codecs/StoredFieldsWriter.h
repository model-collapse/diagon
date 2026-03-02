// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/IndexOutput.h"
#include "diagon/util/BytesRef.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * StoredFieldsWriter - Writes stored fields to disk with LZ4 block compression
 *
 * Format (V2 - LZ4 compressed blocks):
 *
 * .fdx (index) file per segment:
 *   - Header (codec name, version=2)
 *   - numDocs (vInt)
 *   - numBlocks (vInt)
 *   - For each block:
 *     - blockOffset (vLong): start offset of block in .fdt
 *     - numDocsInBlock (vInt): number of docs in this block
 *
 * .fdt (data) file per segment:
 *   - Header (codec name, version=2)
 *   - For each block:
 *     - numDocsInBlock (vInt)
 *     - rawLength (vInt): uncompressed size in bytes
 *     - compressedLength (vInt): compressed size in bytes
 *     - compressedData (bytes): LZ4-compressed block
 *
 *   Within each block's decompressed data:
 *     - For each doc:
 *       - numFields (vInt)
 *       - For each field:
 *         - fieldNumber (vInt)
 *         - fieldType (byte): 0=STRING, 1=INT, 2=LONG
 *         - value (type-dependent):
 *           - STRING: length (vInt) + UTF-8 bytes
 *           - INT: int32 (vInt)
 *           - LONG: int64 (vLong)
 *
 * Based on: org.apache.lucene.codecs.compressing.CompressingStoredFieldsWriter
 */
class StoredFieldsWriter {
public:
    /**
     * Field types for stored fields
     */
    enum class FieldType : uint8_t {
        STRING = 0,
        INT = 1,
        LONG = 2
    };

    /**
     * Constructor
     * @param segmentName Segment name (e.g., "_0")
     */
    explicit StoredFieldsWriter(const std::string& segmentName);

    /**
     * Start writing a document
     * Must be called before writing any fields for this document
     */
    void startDocument();

    /**
     * Finish writing a document
     * Called after all fields for this document have been written
     */
    void finishDocument();

    /**
     * Write a string field
     */
    void writeField(const index::FieldInfo& fieldInfo, const std::string& value);

    /**
     * Write an int field
     */
    void writeField(const index::FieldInfo& fieldInfo, int32_t value);

    /**
     * Write a long field
     */
    void writeField(const index::FieldInfo& fieldInfo, int64_t value);

    /**
     * Finish writing all documents
     * @param numDocs Total number of documents written (for validation)
     */
    void finish(int numDocs);

    /**
     * Flush to output streams
     * @param dataOut Output stream for .fdt file
     * @param indexOut Output stream for .fdx file
     */
    void flush(store::IndexOutput& dataOut, store::IndexOutput& indexOut);

    /**
     * Get RAM bytes used
     */
    int64_t ramBytesUsed() const;

    /**
     * Close and clean up
     */
    void close();

    /** Number of documents per compressed block */
    static constexpr int BLOCK_SIZE = 16;

private:
    /**
     * Stored field value
     */
    struct StoredField {
        int32_t fieldNumber;
        FieldType fieldType;
        std::string stringValue;
        int64_t numericValue{0};  // For INT and LONG types

        StoredField(int32_t num, FieldType type)
            : fieldNumber(num)
            , fieldType(type) {}
    };

    /**
     * Per-document buffer
     */
    struct DocumentBuffer {
        std::vector<StoredField> fields;
    };

    /**
     * Block index entry (offset + doc count) for .fdx
     */
    struct BlockEntry {
        int64_t offset;       // Start offset of block in .fdt
        int numDocsInBlock;   // Number of docs in this block
    };

    std::string segmentName_;

    // Buffer for current document
    std::vector<StoredField> currentDocument_;

    // All documents buffered in RAM
    std::vector<DocumentBuffer> documents_;

    // Number of documents written
    int numDocs_{0};

    // Whether we're currently writing a document
    bool inDocument_{false};

    // Whether finish() has been called
    bool finished_{false};

    // Incremental RAM usage tracking (avoids O(n^2) recomputation)
    int64_t bytesUsed_{0};

    /**
     * Write header to output stream
     */
    void writeHeader(store::IndexOutput& out);

    /**
     * Write index file (.fdx) with block-level entries
     */
    void writeIndex(store::IndexOutput& indexOut, const std::vector<BlockEntry>& blocks);

    /**
     * Write data file (.fdt) with LZ4-compressed blocks
     * @return Block entries for .fdx index
     */
    std::vector<BlockEntry> writeData(store::IndexOutput& dataOut);

    /**
     * Serialize a range of documents into a raw byte buffer.
     * @param startDoc First document index (in documents_)
     * @param count Number of documents to serialize
     * @return Raw serialized bytes
     */
    std::vector<uint8_t> serializeBlock(int startDoc, int count);

    /**
     * Encode a vInt into a byte buffer.
     */
    static void encodeVInt(std::vector<uint8_t>& buf, int32_t i);

    /**
     * Encode a vLong into a byte buffer.
     */
    static void encodeVLong(std::vector<uint8_t>& buf, int64_t l);

    /**
     * Encode a string (length-prefixed) into a byte buffer.
     */
    static void encodeString(std::vector<uint8_t>& buf, const std::string& s);
};

}  // namespace codecs
}  // namespace diagon
