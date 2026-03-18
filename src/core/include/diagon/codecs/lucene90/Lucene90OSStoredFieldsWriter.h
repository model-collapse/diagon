// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/CodecUtil.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <string>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Lucene 90-compatible stored fields writer.
 *
 * Based on: org.apache.lucene.codecs.lucene90.compressing.Lucene90CompressingStoredFieldsWriter
 *
 * Produces 3 files:
 *   .fdt — compressed field data (LZ4 chunks with StoredFieldsInts headers)
 *   .fdx — fields index (DirectMonotonic packed arrays for chunk lookup)
 *   .fdm — fields index metadata (block headers for DirectMonotonic)
 *
 * Uses BEST_SPEED mode: LZ4, maxDocsPerChunk=1024, chunkSize=81920 (80KB).
 *
 * Chunk format in .fdt:
 *   docBase (VInt)
 *   numBufferedDocs<<2 | dirtyBit<<1 | slicedBit (VInt)
 *   numStoredFields[] (StoredFieldsInts or VInt for single-doc)
 *   lengths[] (StoredFieldsInts or VInt for single-doc)
 *   LZ4-compressed field data
 *
 * Per-field encoding in compressed data:
 *   VLong(fieldNumber << TYPE_BITS | typeCode)
 *   Type-specific value:
 *     STRING(0):  VInt(len) + UTF-8 bytes (via writeString)
 *     BYTE_ARR(1): VInt(len) + bytes
 *     INT(2):     ZInt (zigzag-encoded VInt)
 *     FLOAT(3):   ZFloat (variable-length float)
 *     LONG(4):    TLong (timestamp-aware variable-length long)
 *     DOUBLE(5):  ZDouble (variable-length double)
 */
class Lucene90OSStoredFieldsWriter {
public:
    // Type codes (3 bits)
    static constexpr int STRING = 0x00;
    static constexpr int BYTE_ARR = 0x01;
    static constexpr int NUMERIC_INT = 0x02;
    static constexpr int NUMERIC_FLOAT = 0x03;
    static constexpr int NUMERIC_LONG = 0x04;
    static constexpr int NUMERIC_DOUBLE = 0x05;
    static constexpr int TYPE_BITS = 3;
    static constexpr int TYPE_MASK = (1 << TYPE_BITS) - 1;  // 0x07

    // Version
    static constexpr int VERSION_START = 1;
    static constexpr int VERSION_CURRENT = VERSION_START;
    static constexpr int META_VERSION_START = 0;

    // File extensions
    static constexpr const char* FIELDS_EXTENSION = "fdt";
    static constexpr const char* INDEX_EXTENSION = "fdx";
    static constexpr const char* META_EXTENSION = "fdm";

    // Codec names
    static constexpr const char* INDEX_CODEC_NAME = "Lucene90FieldsIndex";
    static constexpr const char* META_CODEC_NAME = "Lucene90FieldsIndexMeta";

    // BEST_SPEED mode parameters
    static constexpr int CHUNK_SIZE = 81920;          // 80KB
    static constexpr int MAX_DOCS_PER_CHUNK = 1024;
    static constexpr int BLOCK_SHIFT = 10;            // 1024 values per DM block

    // Timestamp encoding constants for TLong
    static constexpr int64_t SECOND = 1000L;
    static constexpr int64_t HOUR = 60 * 60 * SECOND;
    static constexpr int64_t DAY = 24 * HOUR;
    static constexpr int SECOND_ENCODING = 0x40;
    static constexpr int HOUR_ENCODING = 0x80;
    static constexpr int DAY_ENCODING = 0xC0;

    /**
     * Construct writer and open .fdt/.fdm output files.
     *
     * @param dir Directory to write files in
     * @param segmentName Segment name (e.g., "_0")
     * @param segmentID 16-byte segment identifier
     * @param formatName Codec name for .fdt header (e.g., "Lucene90StoredFieldsFastData")
     */
    Lucene90OSStoredFieldsWriter(store::Directory& dir, const std::string& segmentName,
                                  const uint8_t* segmentID, const std::string& formatName);

    ~Lucene90OSStoredFieldsWriter();

    // Document lifecycle
    void startDocument();
    void finishDocument();

    // Field writers
    void writeField(const index::FieldInfo& info, const std::string& value);
    void writeField(const index::FieldInfo& info, int32_t value);
    void writeField(const index::FieldInfo& info, int64_t value);

    /**
     * Finish writing all documents and write index files (.fdx/.fdm).
     * @param numDocs Total document count (for validation)
     */
    void finish(int numDocs);

    /** Close all output streams. */
    void close();

    /** Get list of files created. */
    std::vector<std::string> getFiles() const;

    /** RAM bytes used by buffered docs. */
    int64_t ramBytesUsed() const;

private:
    std::string segmentName_;
    std::string formatName_;
    uint8_t segmentID_[16];

    // Output streams
    store::Directory& dir_;
    std::unique_ptr<store::IndexOutput> metaStream_;    // .fdm
    std::unique_ptr<store::IndexOutput> fieldsStream_;  // .fdt

    // Chunk buffering
    std::vector<uint8_t> bufferedDocs_;  // Raw field data buffer
    std::vector<int> numStoredFields_;   // Per-doc stored field count
    std::vector<int> endOffsets_;        // Per-doc end offset in bufferedDocs_
    int docBase_;
    int numBufferedDocs_;
    int numStoredFieldsInDoc_;

    // Index tracking (in-memory, written at finish())
    std::vector<int> chunkDocCounts_;     // Docs per chunk
    std::vector<int64_t> chunkStartFPs_;  // Start FP in .fdt per chunk

    // Statistics
    int64_t numChunks_;
    int64_t numDirtyChunks_;
    int64_t numDirtyDocs_;

    // Internal methods
    bool triggerFlush() const;
    void flushChunk(bool force);
    void writeHeader(int docBase, int numBufferedDocs, const int* numStoredFields,
                     const int* lengths, bool sliced, bool dirtyChunk);
    void writeFieldsIndex(int numDocs);

    // Encoding helpers (write to bufferedDocs_)
    void bufWriteByte(uint8_t b);
    void bufWriteVInt(int32_t i);
    void bufWriteVLong(int64_t l);
    void bufWriteString(const std::string& s);
    void bufWriteZInt(int32_t i);
    void bufWriteTLong(int64_t l);

    /** ZigZag encode a 32-bit value. */
    static int32_t zigZagEncode32(int32_t i) {
        return (i >> 31) ^ (i << 1);
    }

    /** ZigZag encode a 64-bit value. */
    static int64_t zigZagEncode64(int64_t l) {
        return (l >> 63) ^ (l << 1);
    }
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
