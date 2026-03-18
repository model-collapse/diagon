// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsReader.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/codecs/lucene90/StoredFieldsInts.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/packed/DirectMonotonicWriter.h"  // for DirectMonotonicReader

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef HAVE_LZ4
#    include <lz4.h>
#endif

namespace diagon {
namespace codecs {
namespace lucene90 {

using Writer = Lucene90OSStoredFieldsWriter;

// ==================== Constructor / Destructor ====================

Lucene90OSStoredFieldsReader::Lucene90OSStoredFieldsReader(store::Directory& dir,
                                                             const std::string& segmentName,
                                                             const uint8_t* segmentID,
                                                             const index::FieldInfos& fieldInfos,
                                                             const std::string& formatName)
    : dir_(dir)
    , fieldInfos_(fieldInfos)
    , numDocs_(0)
    , chunkSize_(0)
    , numChunks_(0)
    , numDirtyChunks_(0)
    , numDirtyDocs_(0)
    , maxPointer_(0)
    , blockShift_(0)
    , docBasesMetaFP_(0)
    , docBasesDataFP_(0)
    , startPointersMetaFP_(0)
    , startPointersDataFP_(0) {
    // Open .fdt
    std::string fdtName = segmentName + "." + Writer::FIELDS_EXTENSION;
    fieldsStream_ = dir_.openInput(fdtName, store::IOContext::DEFAULT);
    CodecUtil::checkIndexHeader(*fieldsStream_, formatName, Writer::VERSION_START,
                                Writer::VERSION_CURRENT, segmentID, "");
    chunkSize_ = fieldsStream_->readVInt();

    // Open .fdm
    std::string fdmName = segmentName + "." + Writer::META_EXTENSION;
    metaStream_ = dir_.openInput(fdmName, store::IOContext::DEFAULT);
    CodecUtil::checkIndexHeader(*metaStream_, Writer::META_CODEC_NAME, Writer::META_VERSION_START,
                                Writer::META_VERSION_START, segmentID, "");

    // Open .fdx
    std::string fdxName = segmentName + "." + Writer::INDEX_EXTENSION;
    indexStream_ = dir_.openInput(fdxName, store::IOContext::DEFAULT);
    CodecUtil::checkIndexHeader(*indexStream_, Writer::INDEX_CODEC_NAME, Writer::META_VERSION_START,
                                Writer::META_VERSION_START, segmentID, "");

    // Read index from .fdm/.fdx
    readIndex();
}

Lucene90OSStoredFieldsReader::~Lucene90OSStoredFieldsReader() {
    close();
}

void Lucene90OSStoredFieldsReader::readIndex() {
    // Read metadata from .fdm
    numDocs_ = metaStream_->readInt();
    blockShift_ = metaStream_->readInt();
    int numChunksPlus1 = metaStream_->readVInt();

    // Base data pointer for DM arrays in .fdx (current position, after header)
    int64_t baseDataFP = indexStream_->getFilePointer();

    // Read DirectMonotonic for doc bases
    // DM metadata is in .fdm (sequential), DM packed data is in .fdx
    int64_t docBasesMetaFP = metaStream_->getFilePointer();
    chunkIndex_.docBases = util::packed::DirectMonotonicReader::readAll(
        metaStream_.get(), indexStream_.get(), baseDataFP, blockShift_, numChunksPlus1,
        docBasesMetaFP);

    // Read relative offset of FP data in .fdx (written between the two DM metadata blocks)
    int64_t startPointersDataOffset = metaStream_->readLong();
    int64_t startPointersBaseDataFP = baseDataFP + startPointersDataOffset;

    // Read DirectMonotonic for start pointers
    int64_t startPointersMetaFP = metaStream_->getFilePointer();
    chunkIndex_.startPointers = util::packed::DirectMonotonicReader::readAll(
        metaStream_.get(), indexStream_.get(), startPointersBaseDataFP, blockShift_,
        numChunksPlus1, startPointersMetaFP);

    // Read trailing metadata
    int64_t indexDataLength = metaStream_->readLong();
    maxPointer_ = metaStream_->readLong();

    // Read statistics
    numChunks_ = metaStream_->readVLong();
    numDirtyChunks_ = metaStream_->readVLong();
    numDirtyDocs_ = metaStream_->readVLong();

    chunkIndex_.numChunks = numChunksPlus1 - 1;
    (void)indexDataLength;
}

// ==================== Document Access ====================

Lucene90OSStoredFieldsReader::DocumentFields Lucene90OSStoredFieldsReader::document(int docID) {
    if (docID < 0 || docID >= numDocs_) {
        throw std::runtime_error("docID out of range: " + std::to_string(docID) +
                                 " (numDocs=" + std::to_string(numDocs_) + ")");
    }

    // Find which chunk contains this docID
    int chunkIdx = findChunk(docID);

    // Use cached chunk if available
    if (chunkIdx != cachedChunkIdx_) {
        cachedData_ = readChunk(chunkIdx, cachedNumStoredFields_, cachedLengths_);
        cachedChunkIdx_ = chunkIdx;
    }

    // Find the doc's offset within the decompressed data
    int docBase = static_cast<int>(chunkIndex_.docBases[chunkIdx]);
    int docInChunk = docID - docBase;

    int offset = 0;
    for (int i = 0; i < docInChunk; i++) {
        offset += static_cast<int>(cachedLengths_[i]);
    }
    int length = static_cast<int>(cachedLengths_[docInChunk]);

    return parseDocument(cachedData_.data(), static_cast<int>(cachedData_.size()), offset, length);
}

int Lucene90OSStoredFieldsReader::findChunk(int docID) const {
    // Binary search on docBases: find largest i such that docBases[i] <= docID
    const auto& bases = chunkIndex_.docBases;
    int lo = 0, hi = chunkIndex_.numChunks - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (bases[mid] <= docID) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

std::vector<uint8_t> Lucene90OSStoredFieldsReader::readChunk(int chunkIdx,
                                                               std::vector<int64_t>& numStoredFields,
                                                               std::vector<int64_t>& lengths) {
    // Seek to chunk start in .fdt
    int64_t chunkFP = chunkIndex_.startPointers[chunkIdx];
    fieldsStream_->seek(chunkFP);

    // Read chunk header
    fieldsStream_->readVInt();  // docBase (not needed — we already know it from index)
    int token = fieldsStream_->readVInt();
    int numDocs = token >> 2;
    bool dirty = (token & 2) != 0;
    bool sliced = (token & 1) != 0;
    (void)dirty;  // not used for reading
    (void)sliced; // we don't support sliced chunks

    // Read numStoredFields and lengths
    numStoredFields.resize(numDocs);
    lengths.resize(numDocs);

    if (numDocs == 1) {
        numStoredFields[0] = fieldsStream_->readVInt();
        lengths[0] = fieldsStream_->readVInt();
    } else {
        StoredFieldsInts::readInts(*fieldsStream_, numDocs, numStoredFields, 0);
        StoredFieldsInts::readInts(*fieldsStream_, numDocs, lengths, 0);
    }

    // Compute total decompressed size
    int64_t totalDecompressed = 0;
    for (int i = 0; i < numDocs; i++) {
        totalDecompressed += lengths[i];
    }

    // Compute compressed size from file pointer arithmetic
    // Next chunk starts at chunkIndex_.startPointers[chunkIdx + 1]
    // For last chunk, compressed data ends at maxPointer_ (before footer)
    int64_t compressedStart = fieldsStream_->getFilePointer();
    int64_t compressedEnd;
    if (chunkIdx + 1 < chunkIndex_.numChunks) {
        compressedEnd = chunkIndex_.startPointers[chunkIdx + 1];
    } else {
        compressedEnd = maxPointer_;
    }
    int64_t compressedSize = compressedEnd - compressedStart;

    // Read compressed data
    std::vector<uint8_t> compressedBuf(compressedSize);
    fieldsStream_->readBytes(compressedBuf.data(), compressedSize);

    // Decompress
    std::vector<uint8_t> decompressed(totalDecompressed);

    if (totalDecompressed == 0) {
        return decompressed;
    }

#ifdef HAVE_LZ4
    int result = LZ4_decompress_safe(reinterpret_cast<const char*>(compressedBuf.data()),
                                      reinterpret_cast<char*>(decompressed.data()),
                                      static_cast<int>(compressedSize),
                                      static_cast<int>(totalDecompressed));
    if (result < 0) {
        throw std::runtime_error("LZ4 decompression failed for chunk " + std::to_string(chunkIdx));
    }
#else
    // No LZ4 — assume data is uncompressed
    if (compressedSize != totalDecompressed) {
        throw std::runtime_error("LZ4 not available and data appears compressed");
    }
    std::memcpy(decompressed.data(), compressedBuf.data(), compressedSize);
#endif

    return decompressed;
}

Lucene90OSStoredFieldsReader::DocumentFields
Lucene90OSStoredFieldsReader::parseDocument(const uint8_t* data, int dataLen, int offset,
                                              int length) {
    DocumentFields fields;
    int pos = offset;
    int end = offset + length;

    while (pos < end) {
        // Read field header: VLong(fieldNumber << TYPE_BITS | typeCode)
        int64_t infoAndBits = decodeVLong(data, dataLen, pos);
        int fieldNumber = static_cast<int>(infoAndBits >> Writer::TYPE_BITS);
        int typeCode = static_cast<int>(infoAndBits & Writer::TYPE_MASK);

        // Look up field name
        std::string fieldName;
        const auto* fi = fieldInfos_.fieldInfo(fieldNumber);
        if (fi) {
            fieldName = fi->name;
        } else {
            fieldName = "_field_" + std::to_string(fieldNumber);
        }

        // Decode value based on type
        switch (typeCode) {
            case Writer::STRING: {
                std::string value = decodeString(data, dataLen, pos);
                fields[fieldName] = std::move(value);
                break;
            }
            case Writer::BYTE_ARR: {
                // Read byte array as string
                std::string value = decodeString(data, dataLen, pos);
                fields[fieldName] = std::move(value);
                break;
            }
            case Writer::NUMERIC_INT: {
                int32_t value = decodeZInt(data, dataLen, pos);
                fields[fieldName] = value;
                break;
            }
            case Writer::NUMERIC_LONG: {
                int64_t value = decodeTLong(data, dataLen, pos);
                fields[fieldName] = value;
                break;
            }
            case Writer::NUMERIC_FLOAT: {
                // Read as 4 bytes, store as int32_t (float bits)
                // TODO: proper float support
                int32_t bits = decodeVInt(data, dataLen, pos);
                fields[fieldName] = bits;
                break;
            }
            case Writer::NUMERIC_DOUBLE: {
                // Read as 8 bytes, store as int64_t (double bits)
                // TODO: proper double support
                int64_t bits = decodeVLong(data, dataLen, pos);
                fields[fieldName] = bits;
                break;
            }
            default:
                throw std::runtime_error("Unknown stored field type: " + std::to_string(typeCode));
        }
    }

    return fields;
}

// ==================== Decoding Helpers ====================

int32_t Lucene90OSStoredFieldsReader::decodeVInt(const uint8_t* data, int dataLen, int& pos) {
    uint32_t result = 0;
    int shift = 0;
    while (pos < dataLen) {
        uint8_t b = data[pos++];
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return static_cast<int32_t>(result);
}

int64_t Lucene90OSStoredFieldsReader::decodeVLong(const uint8_t* data, int dataLen, int& pos) {
    uint64_t result = 0;
    int shift = 0;
    while (pos < dataLen) {
        uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return static_cast<int64_t>(result);
}

std::string Lucene90OSStoredFieldsReader::decodeString(const uint8_t* data, int dataLen, int& pos) {
    int32_t len = decodeVInt(data, dataLen, pos);
    if (pos + len > dataLen) {
        throw std::runtime_error("String extends beyond data boundary");
    }
    std::string result(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return result;
}

int32_t Lucene90OSStoredFieldsReader::decodeZInt(const uint8_t* data, int dataLen, int& pos) {
    int32_t zigzag = decodeVInt(data, dataLen, pos);
    return zigZagDecode32(zigzag);
}

int64_t Lucene90OSStoredFieldsReader::decodeTLong(const uint8_t* data, int dataLen, int& pos) {
    // TLong: timestamp-aware variable-length long decoding
    if (pos >= dataLen) {
        throw std::runtime_error("TLong: unexpected end of data");
    }

    int header = data[pos++];
    int encoding = header & 0xC0;  // top 2 bits: time unit
    int valuePart = header & 0x3F; // bottom 6 bits

    int64_t zigzag;
    if (valuePart == 0x20) {
        // Overflow: value encoded as following VLong
        zigzag = decodeVLong(data, dataLen, pos);
    } else {
        // Value fits in 5 bits (0x00-0x1F)
        zigzag = valuePart;
    }

    int64_t value = zigZagDecode64(zigzag);

    // Apply time unit multiplier
    switch (encoding) {
        case Writer::DAY_ENCODING:
            return value * Writer::DAY;
        case Writer::HOUR_ENCODING:
            return value * Writer::HOUR;
        case Writer::SECOND_ENCODING:
            return value * Writer::SECOND;
        default:
            return value;
    }
}

void Lucene90OSStoredFieldsReader::close() {
    fieldsStream_.reset();
    metaStream_.reset();
    indexStream_.reset();
}

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
