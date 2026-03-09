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
 * Supports two modes:
 *
 * 1. **Buffered mode** (DWPT flush): Constructor takes segment name only.
 *    All documents buffered in RAM. Call finish() then flush(dataOut, indexOut).
 *    Suitable for small segments (1K-5K docs).
 *
 * 2. **Streaming mode** (merge): Constructor takes segment name + output streams.
 *    Documents flushed to disk in BLOCK_SIZE increments as they're added.
 *    RAM usage is O(BLOCK_SIZE) regardless of total docs. Call finish() at end.
 *    Required for large merges (100M+ docs) to prevent OOM.
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
     * Constructor (buffered mode — for DWPT flush of small segments)
     * @param segmentName Segment name (e.g., "_0")
     */
    explicit StoredFieldsWriter(const std::string& segmentName);

    /**
     * Constructor (streaming mode — for merge of large segments)
     * Documents are flushed to disk in BLOCK_SIZE increments.
     * RAM usage: O(BLOCK_SIZE * avg_doc_size) ≈ 8KB instead of O(N * avg_doc_size).
     * @param segmentName Segment name
     * @param dataOut Output stream for .fdt file (must remain valid until finish())
     * @param indexOut Output stream for .fdx file (must remain valid until finish())
     */
    StoredFieldsWriter(const std::string& segmentName,
                       store::IndexOutput& dataOut,
                       store::IndexOutput& indexOut);

    /**
     * Start writing a document
     */
    void startDocument();

    /**
     * Finish writing a document.
     * In streaming mode, may flush a completed block to disk.
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
     * Finish writing all documents.
     * In streaming mode: flushes remaining partial block + writes index.
     * @param numDocs Total number of documents written (for validation)
     */
    void finish(int numDocs);

    /**
     * Flush to output streams (buffered mode only).
     * In streaming mode, data is already on disk — this is a no-op.
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
        int64_t offset;      // Start offset of block in .fdt
        int numDocsInBlock;  // Number of docs in this block
    };

    std::string segmentName_;

    // Buffer for current document's fields
    std::vector<StoredField> currentDocument_;

    // Buffered mode: all documents held in RAM until flush()
    std::vector<DocumentBuffer> documents_;

    // Streaming mode: block buffer (max BLOCK_SIZE docs)
    std::vector<DocumentBuffer> blockBuffer_;

    // Streaming mode: output streams (non-owning, null in buffered mode)
    store::IndexOutput* dataOut_{nullptr};
    store::IndexOutput* indexOut_{nullptr};

    // Block entries accumulated during streaming (small: ~20 bytes per 16 docs)
    std::vector<BlockEntry> streamBlocks_;

    // Whether we're in streaming mode
    bool streaming_{false};

    // Number of documents written
    int numDocs_{0};

    // Whether we're currently writing a document
    bool inDocument_{false};

    // Whether finish() has been called
    bool finished_{false};

    // Incremental RAM usage tracking
    int64_t bytesUsed_{0};

    // Whether the data header has been written (streaming mode)
    bool headerWritten_{false};

    /**
     * Write header to output stream
     */
    void writeHeader(store::IndexOutput& out);

    /**
     * Write index file (.fdx) with block-level entries
     */
    void writeIndex(store::IndexOutput& indexOut, const std::vector<BlockEntry>& blocks);

    /**
     * Write data file (.fdt) with LZ4-compressed blocks (buffered mode)
     * @return Block entries for .fdx index
     */
    std::vector<BlockEntry> writeData(store::IndexOutput& dataOut);

    /**
     * Flush current block buffer to disk (streaming mode).
     * Serializes + compresses + writes to dataOut_, records BlockEntry.
     */
    void flushBlockToDisk();

    /**
     * Serialize documents from a vector into a raw byte buffer.
     * @param docs Documents to serialize
     * @param startIdx Start index in docs
     * @param count Number of documents to serialize
     * @return Raw serialized bytes
     */
    std::vector<uint8_t> serializeDocs(const std::vector<DocumentBuffer>& docs,
                                        int startIdx, int count);

    /**
     * Serialize a range of documents from documents_ (buffered mode compat).
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

    /**
     * Write a compressed block to an output stream.
     * @return BlockEntry with offset and doc count
     */
    BlockEntry writeCompressedBlock(store::IndexOutput& out,
                                     const std::vector<DocumentBuffer>& docs,
                                     int startIdx, int count);
};

}  // namespace codecs
}  // namespace diagon
