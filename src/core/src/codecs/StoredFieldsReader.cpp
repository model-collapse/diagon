// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsReader.h"

#include "diagon/store/IOContext.h"

#include <cstring>
#include <stdexcept>

#ifdef HAVE_LZ4
#include <lz4.h>
#endif

namespace diagon {
namespace codecs {

// Codec constants (must match StoredFieldsWriter)
static const std::string CODEC_NAME = "DiagonStoredFields";
static const int VERSION_V1 = 1;  // Uncompressed per-doc format
static const int VERSION_V2 = 2;  // LZ4 block compressed format

// Field types (must match StoredFieldsWriter)
enum class FieldType : uint8_t {
    STRING = 0,
    INT = 1,
    LONG = 2
};

StoredFieldsReader::StoredFieldsReader(store::Directory* directory, const std::string& segmentName,
                                       const index::FieldInfos& fieldInfos)
    : directory_(directory)
    , segmentName_(segmentName)
    , fieldInfos_(fieldInfos) {
    // Open .fdt (data) file
    dataInput_ = directory_->openInput(segmentName + ".fdt", store::IOContext::READ);

    // Open .fdx (index) file
    indexInput_ = directory_->openInput(segmentName + ".fdx", store::IOContext::READ);

    // Read index to build block/offset structure
    readIndex();
}

StoredFieldsReader::~StoredFieldsReader() {
    if (!closed_) {
        close();
    }
}

int StoredFieldsReader::verifyHeader(store::IndexInput& input, const std::string& expectedCodec) {
    // Read codec name
    std::string codec = input.readString();
    if (codec != expectedCodec) {
        throw std::runtime_error("Invalid codec: expected " + expectedCodec + ", got " + codec);
    }

    // Read version
    int version = input.readVInt();
    if (version != VERSION_V1 && version != VERSION_V2) {
        throw std::runtime_error("Invalid version: " + std::to_string(version));
    }

    return version;
}

void StoredFieldsReader::readIndex() {
    // Verify index file header and get version
    int indexVersion = verifyHeader(*indexInput_, CODEC_NAME);

    // Verify data file header
    int dataVersion = verifyHeader(*dataInput_, CODEC_NAME);

    if (indexVersion != dataVersion) {
        throw std::runtime_error("Version mismatch between .fdx (" + std::to_string(indexVersion) +
                                 ") and .fdt (" + std::to_string(dataVersion) + ")");
    }

    version_ = indexVersion;

    if (version_ == VERSION_V1) {
        readIndexV1();
    } else {
        readIndexV2();
    }
}

void StoredFieldsReader::readIndexV1() {
    // V1: per-doc offsets
    numDocs_ = indexInput_->readVInt();

    offsets_.reserve(numDocs_);
    for (int i = 0; i < numDocs_; i++) {
        int64_t offset = indexInput_->readVLong();
        offsets_.push_back(offset);
    }
}

void StoredFieldsReader::readIndexV2() {
    // V2: block-level index
    numDocs_ = indexInput_->readVInt();
    int numBlocks = indexInput_->readVInt();

    blocks_.reserve(numBlocks);
    int firstDocID = 0;
    for (int i = 0; i < numBlocks; i++) {
        BlockEntry entry;
        entry.offset = indexInput_->readVLong();
        entry.numDocsInBlock = indexInput_->readVInt();
        entry.firstDocID = firstDocID;
        firstDocID += entry.numDocsInBlock;
        blocks_.push_back(entry);
    }
}

int StoredFieldsReader::findBlock(int docID) const {
    // Binary search for the block containing docID
    int lo = 0;
    int hi = static_cast<int>(blocks_.size()) - 1;

    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (blocks_[mid].firstDocID <= docID) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    return lo;
}

const std::vector<uint8_t>& StoredFieldsReader::decompressBlock(int blockIdx) {
    // Check cache
    if (blockIdx == cachedBlockIdx_) {
        return cachedBlockData_;
    }

    const auto& block = blocks_[blockIdx];

    // Seek to block in .fdt
    dataInput_->seek(block.offset);

    // Read block header
    int numDocsInBlock = dataInput_->readVInt();
    int rawLength = dataInput_->readVInt();
    int compressedLength = dataInput_->readVInt();

    if (numDocsInBlock != block.numDocsInBlock) {
        throw std::runtime_error("Block doc count mismatch: index says " +
                                 std::to_string(block.numDocsInBlock) + ", data says " +
                                 std::to_string(numDocsInBlock));
    }

    // Read compressed data
    std::vector<uint8_t> compressed(compressedLength);
    dataInput_->readBytes(compressed.data(), compressedLength);

    // Decompress
    cachedBlockData_.resize(rawLength);

    if (compressedLength == rawLength) {
        // Stored uncompressed (no LZ4 available at write time, or incompressible data)
        std::memcpy(cachedBlockData_.data(), compressed.data(), rawLength);
    } else {
#ifdef HAVE_LZ4
        int decompressedSize = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed.data()),
            reinterpret_cast<char*>(cachedBlockData_.data()),
            compressedLength, rawLength);

        if (decompressedSize != rawLength) {
            throw std::runtime_error(
                "LZ4 decompression failed: expected " + std::to_string(rawLength) +
                " bytes, got " + std::to_string(decompressedSize));
        }
#else
        throw std::runtime_error("LZ4 compressed data but HAVE_LZ4 not defined");
#endif
    }

    cachedBlockIdx_ = blockIdx;
    return cachedBlockData_;
}

// ==================== VInt/VLong/String Decoding Helpers ====================

int32_t StoredFieldsReader::decodeVInt(const uint8_t* data, int dataLen, int& pos) {
    uint32_t result = 0;
    int shift = 0;
    while (pos < dataLen) {
        uint8_t b = data[pos++];
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            return static_cast<int32_t>(result);
        }
        shift += 7;
        if (shift > 28) {
            throw std::runtime_error("VInt too large");
        }
    }
    throw std::runtime_error("Unexpected end of data reading VInt");
}

int64_t StoredFieldsReader::decodeVLong(const uint8_t* data, int dataLen, int& pos) {
    uint64_t result = 0;
    int shift = 0;
    while (pos < dataLen) {
        uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            return static_cast<int64_t>(result);
        }
        shift += 7;
        if (shift > 63) {
            throw std::runtime_error("VLong too large");
        }
    }
    throw std::runtime_error("Unexpected end of data reading VLong");
}

std::string StoredFieldsReader::decodeString(const uint8_t* data, int dataLen, int& pos) {
    int32_t len = decodeVInt(data, dataLen, pos);
    if (len < 0 || pos + len > dataLen) {
        throw std::runtime_error("Invalid string length: " + std::to_string(len));
    }
    std::string result(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return result;
}

StoredFieldsReader::DocumentFields StoredFieldsReader::parseDocument(const uint8_t* data,
                                                                      int dataLen,
                                                                      int skipCount) {
    int pos = 0;

    // Skip over previous documents in this block
    for (int i = 0; i < skipCount; i++) {
        int32_t numFields = decodeVInt(data, dataLen, pos);
        for (int32_t f = 0; f < numFields; f++) {
            decodeVInt(data, dataLen, pos);   // fieldNumber
            if (pos >= dataLen) {
                throw std::runtime_error("Unexpected end of block data");
            }
            uint8_t typeCode = data[pos++];   // fieldType
            FieldType fieldType = static_cast<FieldType>(typeCode);

            switch (fieldType) {
                case FieldType::STRING:
                    decodeString(data, dataLen, pos);
                    break;
                case FieldType::INT:
                    decodeVInt(data, dataLen, pos);
                    break;
                case FieldType::LONG:
                    decodeVLong(data, dataLen, pos);
                    break;
                default:
                    throw std::runtime_error("Unknown field type: " + std::to_string(typeCode));
            }
        }
    }

    // Now parse the target document
    DocumentFields fields;
    int32_t numFields = decodeVInt(data, dataLen, pos);

    for (int32_t f = 0; f < numFields; f++) {
        int32_t fieldNumber = decodeVInt(data, dataLen, pos);

        // Look up field name from FieldInfos
        const index::FieldInfo* fieldInfo = nullptr;
        for (const auto& fi : fieldInfos_) {
            if (fi.number == fieldNumber) {
                fieldInfo = &fi;
                break;
            }
        }

        if (!fieldInfo) {
            throw std::runtime_error("Unknown field number: " + std::to_string(fieldNumber));
        }

        if (pos >= dataLen) {
            throw std::runtime_error("Unexpected end of block data");
        }
        uint8_t typeCode = data[pos++];
        FieldType fieldType = static_cast<FieldType>(typeCode);

        switch (fieldType) {
            case FieldType::STRING: {
                std::string value = decodeString(data, dataLen, pos);
                fields[fieldInfo->name] = value;
                break;
            }
            case FieldType::INT: {
                int32_t value = decodeVInt(data, dataLen, pos);
                fields[fieldInfo->name] = value;
                break;
            }
            case FieldType::LONG: {
                int64_t value = decodeVLong(data, dataLen, pos);
                fields[fieldInfo->name] = value;
                break;
            }
            default:
                throw std::runtime_error("Unknown field type: " + std::to_string(typeCode));
        }
    }

    return fields;
}

StoredFieldsReader::DocumentFields StoredFieldsReader::document(int docID) {
    if (closed_) {
        throw std::runtime_error("StoredFieldsReader is closed");
    }

    if (docID < 0 || docID >= numDocs_) {
        throw std::runtime_error("Document ID out of range: " + std::to_string(docID));
    }

    if (version_ == VERSION_V1) {
        // V1 legacy path: read uncompressed per-doc data directly from .fdt
        int64_t offset = offsets_[docID];
        dataInput_->seek(offset);

        int numFields = dataInput_->readVInt();

        DocumentFields fields;
        for (int i = 0; i < numFields; i++) {
            int32_t fieldNumber = dataInput_->readVInt();

            const index::FieldInfo* fieldInfo = nullptr;
            for (const auto& fi : fieldInfos_) {
                if (fi.number == fieldNumber) {
                    fieldInfo = &fi;
                    break;
                }
            }

            if (!fieldInfo) {
                throw std::runtime_error("Unknown field number: " + std::to_string(fieldNumber));
            }

            uint8_t typeCode = dataInput_->readByte();
            FieldType fieldType = static_cast<FieldType>(typeCode);

            switch (fieldType) {
                case FieldType::STRING: {
                    std::string value = dataInput_->readString();
                    fields[fieldInfo->name] = value;
                    break;
                }
                case FieldType::INT: {
                    int32_t value = dataInput_->readVInt();
                    fields[fieldInfo->name] = value;
                    break;
                }
                case FieldType::LONG: {
                    int64_t value = dataInput_->readVLong();
                    fields[fieldInfo->name] = value;
                    break;
                }
                default:
                    throw std::runtime_error("Unknown field type: " + std::to_string(typeCode));
            }
        }

        return fields;
    }

    // V2 path: LZ4 block compressed
    int blockIdx = findBlock(docID);
    const auto& block = blocks_[blockIdx];
    int docWithinBlock = docID - block.firstDocID;

    // Decompress the block (cached if same block as last access)
    const auto& decompressed = decompressBlock(blockIdx);

    // Parse the target document within the decompressed block
    return parseDocument(decompressed.data(), static_cast<int>(decompressed.size()),
                         docWithinBlock);
}

void StoredFieldsReader::close() {
    if (closed_) {
        return;
    }

    // IndexInput uses RAII - just reset the unique_ptr
    dataInput_.reset();
    indexInput_.reset();

    closed_ = true;
}

}  // namespace codecs
}  // namespace diagon
