// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * StoredFieldsReader - Reads stored fields from LZ4-compressed .fdt/.fdx files
 *
 * Based on: org.apache.lucene.codecs.compressing.CompressingStoredFieldsReader
 *
 * File Format (V2 - LZ4 compressed blocks):
 * - .fdx (index) file: Maps block index to offset in .fdt file
 * - .fdt (data) file: LZ4-compressed blocks of stored field values
 *
 * Design:
 * - Block-level index: .fdx maps blocks (not individual docs) to .fdt offsets
 * - LZ4 decompression: entire block decompressed on access
 * - Block cache: last decompressed block cached for sequential access
 * - Random access: binary search on block index, then scan within block
 *
 * Usage:
 *   StoredFieldsReader reader(directory, segmentName, fieldInfos);
 *   auto fields = reader.document(docID);
 *   std::string title = std::get<std::string>(fields["title"]);
 *   int64_t count = std::get<int64_t>(fields["count"]);
 *
 * Thread Safety:
 * - NOT thread-safe (clone for each thread)
 */
class StoredFieldsReader {
public:
    /**
     * Field value type (STRING, INT32, or INT64)
     */
    using FieldValue = std::variant<std::string, int32_t, int64_t>;

    /**
     * Document fields (map from field name to value)
     */
    using DocumentFields = std::unordered_map<std::string, FieldValue>;

    /**
     * Constructor - opens .fdt and .fdx files
     *
     * @param directory Directory containing segment files
     * @param segmentName Segment name (e.g., "_0")
     * @param fieldInfos Field metadata for looking up field names
     */
    StoredFieldsReader(store::Directory* directory, const std::string& segmentName,
                       const ::diagon::index::FieldInfos& fieldInfos);

    /**
     * Destructor - closes input files
     */
    ~StoredFieldsReader();

    // Disable copy/move
    StoredFieldsReader(const StoredFieldsReader&) = delete;
    StoredFieldsReader& operator=(const StoredFieldsReader&) = delete;
    StoredFieldsReader(StoredFieldsReader&&) = delete;
    StoredFieldsReader& operator=(StoredFieldsReader&&) = delete;

    /**
     * Read all stored fields for a document
     *
     * @param docID Document ID (0-based)
     * @return Map from field name to field value
     * @throws std::runtime_error if docID out of range
     */
    DocumentFields document(int docID);

    /**
     * Get number of documents
     */
    int numDocs() const { return numDocs_; }

    /**
     * Close input files
     */
    void close();

private:
    /**
     * Block index entry read from .fdx
     */
    struct BlockEntry {
        int64_t offset;       // Start offset of block in .fdt
        int firstDocID;       // First document ID in this block
        int numDocsInBlock;   // Number of docs in this block
    };

    /**
     * Read index file (.fdx) to build block index
     */
    void readIndex();

    /**
     * Read V1 (uncompressed per-doc) index
     */
    void readIndexV1();

    /**
     * Read V2 (LZ4 block compressed) index
     */
    void readIndexV2();

    /**
     * Verify codec header and return version number
     */
    int verifyHeader(store::IndexInput& input, const std::string& expectedCodec);

    /**
     * Find the block index containing the given docID via binary search
     */
    int findBlock(int docID) const;

    /**
     * Decompress a block and cache it. Returns the decompressed data.
     */
    const std::vector<uint8_t>& decompressBlock(int blockIdx);

    /**
     * Parse a single document from a decompressed block buffer.
     * @param data Pointer to decompressed block data
     * @param dataLen Length of decompressed data
     * @param docOffset Offset within decompressed data to start parsing
     * @param skipCount Number of documents to skip before parsing
     * @return Parsed document fields
     */
    DocumentFields parseDocument(const uint8_t* data, int dataLen, int skipCount);

    /**
     * Decode a vInt from a byte buffer, advancing pos.
     */
    static int32_t decodeVInt(const uint8_t* data, int dataLen, int& pos);

    /**
     * Decode a vLong from a byte buffer, advancing pos.
     */
    static int64_t decodeVLong(const uint8_t* data, int dataLen, int& pos);

    /**
     * Decode a string from a byte buffer, advancing pos.
     */
    static std::string decodeString(const uint8_t* data, int dataLen, int& pos);

    // Directory and segment name
    store::Directory* directory_;
    std::string segmentName_;

    // Field metadata
    const ::diagon::index::FieldInfos& fieldInfos_;

    // Input files
    std::unique_ptr<store::IndexInput> dataInput_;   // .fdt file
    std::unique_ptr<store::IndexInput> indexInput_;  // .fdx file

    // Format version (1 = uncompressed, 2 = LZ4 block compressed)
    int version_{0};

    // Block index (V2)
    std::vector<BlockEntry> blocks_;

    // Per-doc offsets (V1 legacy fallback)
    std::vector<int64_t> offsets_;

    // Number of documents
    int numDocs_{0};

    // Decompressed block cache
    int cachedBlockIdx_{-1};
    std::vector<uint8_t> cachedBlockData_;

    // Closed flag
    bool closed_{false};
};

}  // namespace codecs
}  // namespace diagon
