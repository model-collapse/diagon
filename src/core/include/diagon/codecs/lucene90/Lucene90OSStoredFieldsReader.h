// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Lucene 90-compatible stored fields reader.
 *
 * Based on: org.apache.lucene.codecs.lucene90.compressing.Lucene90CompressingStoredFieldsReader
 *
 * Reads 3 files:
 *   .fdt — compressed field data (LZ4 chunks with StoredFieldsInts headers)
 *   .fdx — fields index (DirectMonotonic packed arrays for chunk lookup)
 *   .fdm — fields index metadata (block headers for DirectMonotonic)
 *
 * Thread Safety: NOT thread-safe (clone for each thread)
 */
class Lucene90OSStoredFieldsReader {
public:
    using FieldValue = std::variant<std::string, int32_t, int64_t>;
    using DocumentFields = std::unordered_map<std::string, FieldValue>;

    /**
     * Open and validate .fdt/.fdx/.fdm files.
     *
     * @param dir Directory containing segment files
     * @param segmentName Segment name (e.g., "_0")
     * @param segmentID 16-byte segment identifier
     * @param fieldInfos Field metadata for looking up field names
     * @param formatName Codec name for .fdt header (e.g., "Lucene90StoredFieldsFastData")
     */
    Lucene90OSStoredFieldsReader(store::Directory& dir, const std::string& segmentName,
                                  const uint8_t* segmentID,
                                  const index::FieldInfos& fieldInfos,
                                  const std::string& formatName);

    ~Lucene90OSStoredFieldsReader();

    // Disable copy
    Lucene90OSStoredFieldsReader(const Lucene90OSStoredFieldsReader&) = delete;
    Lucene90OSStoredFieldsReader& operator=(const Lucene90OSStoredFieldsReader&) = delete;

    /**
     * Read all stored fields for a document.
     *
     * @param docID Document ID (0-based)
     * @return Map from field name to field value
     */
    DocumentFields document(int docID);

    int numDocs() const { return numDocs_; }

    void close();

private:
    // Index data (read from .fdx/.fdm at open time)
    struct ChunkIndex {
        std::vector<int64_t> docBases;      // Cumulative doc counts per chunk + sentinel
        std::vector<int64_t> startPointers;  // .fdt file pointers per chunk + sentinel
        int numChunks;
    };

    /** Read .fdm and .fdx to build chunk index. */
    void readIndex();

    /** Find chunk containing docID via binary search on docBases. */
    int findChunk(int docID) const;

    /** Read and decompress a chunk, returning decompressed field data. */
    std::vector<uint8_t> readChunk(int chunkIdx, std::vector<int64_t>& numStoredFields,
                                    std::vector<int64_t>& lengths);

    /** Parse fields for a single doc from decompressed chunk data. */
    DocumentFields parseDocument(const uint8_t* data, int dataLen, int offset, int length);

    // Decoding helpers for decompressed field data
    static int32_t decodeVInt(const uint8_t* data, int dataLen, int& pos);
    static int64_t decodeVLong(const uint8_t* data, int dataLen, int& pos);
    static std::string decodeString(const uint8_t* data, int dataLen, int& pos);
    static int32_t decodeZInt(const uint8_t* data, int dataLen, int& pos);
    static int64_t decodeTLong(const uint8_t* data, int dataLen, int& pos);

    static int32_t zigZagDecode32(int32_t i) { return (static_cast<uint32_t>(i) >> 1) ^ -(i & 1); }
    static int64_t zigZagDecode64(int64_t l) { return (static_cast<uint64_t>(l) >> 1) ^ -(l & 1); }

    // File inputs
    store::Directory& dir_;
    std::unique_ptr<store::IndexInput> fieldsStream_;  // .fdt
    std::unique_ptr<store::IndexInput> metaStream_;     // .fdm
    std::unique_ptr<store::IndexInput> indexStream_;     // .fdx

    // Metadata
    const index::FieldInfos& fieldInfos_;
    int numDocs_;
    int chunkSize_;
    ChunkIndex chunkIndex_;

    // Statistics (from .fdm footer)
    int64_t numChunks_;
    int64_t numDirtyChunks_;
    int64_t numDirtyDocs_;
    int64_t maxPointer_;

    // DirectMonotonic reader state
    int blockShift_;
    int64_t docBasesMetaFP_;
    int64_t docBasesDataFP_;
    int64_t startPointersMetaFP_;
    int64_t startPointersDataFP_;

    // Cache last accessed chunk
    int cachedChunkIdx_{-1};
    std::vector<uint8_t> cachedData_;
    std::vector<int64_t> cachedNumStoredFields_;
    std::vector<int64_t> cachedLengths_;
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
