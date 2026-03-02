// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsWriter.h"

#include <algorithm>
#include <stdexcept>

#ifdef HAVE_LZ4
#include <lz4.h>
#endif

namespace diagon {
namespace codecs {

// Codec constants
static const std::string CODEC_NAME = "DiagonStoredFields";
static const int VERSION = 2;  // V2: LZ4 block compression

// ==================== Constructor ====================

StoredFieldsWriter::StoredFieldsWriter(const std::string& segmentName)
    : segmentName_(segmentName) {}

// ==================== Document Lifecycle ====================

void StoredFieldsWriter::startDocument() {
    if (inDocument_) {
        throw std::runtime_error("Already in document - call finishDocument() first");
    }
    if (finished_) {
        throw std::runtime_error("Writer already finished");
    }

    currentDocument_.clear();
    inDocument_ = true;
}

void StoredFieldsWriter::finishDocument() {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    // Move current document to documents buffer
    DocumentBuffer doc;
    doc.fields = std::move(currentDocument_);
    documents_.push_back(std::move(doc));

    numDocs_++;
    inDocument_ = false;
}

// ==================== Write Fields ====================

void StoredFieldsWriter::writeField(const index::FieldInfo& fieldInfo, const std::string& value) {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    StoredField field(fieldInfo.number, FieldType::STRING);
    field.stringValue = value;
    bytesUsed_ += sizeof(StoredField) + value.size();
    currentDocument_.push_back(std::move(field));
}

void StoredFieldsWriter::writeField(const index::FieldInfo& fieldInfo, int32_t value) {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    StoredField field(fieldInfo.number, FieldType::INT);
    field.numericValue = value;
    bytesUsed_ += sizeof(StoredField);
    currentDocument_.push_back(std::move(field));
}

void StoredFieldsWriter::writeField(const index::FieldInfo& fieldInfo, int64_t value) {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    StoredField field(fieldInfo.number, FieldType::LONG);
    field.numericValue = value;
    bytesUsed_ += sizeof(StoredField);
    currentDocument_.push_back(std::move(field));
}

// ==================== Finish ====================

void StoredFieldsWriter::finish(int numDocs) {
    if (inDocument_) {
        throw std::runtime_error("Still in document - call finishDocument() first");
    }
    if (finished_) {
        throw std::runtime_error("Already finished");
    }

    // Validate document count
    if (numDocs != numDocs_) {
        throw std::runtime_error("Expected " + std::to_string(numDocs) + " documents, but got " +
                                 std::to_string(numDocs_));
    }

    finished_ = true;
}

// ==================== Flush ====================

void StoredFieldsWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& indexOut) {
    if (!finished_) {
        throw std::runtime_error("Must call finish() before flush()");
    }

    // Write data file and collect block entries
    std::vector<BlockEntry> blocks = writeData(dataOut);

    // Write index file
    writeIndex(indexOut, blocks);
}

// ==================== RAM Usage ====================

int64_t StoredFieldsWriter::ramBytesUsed() const {
    return bytesUsed_;
}

// ==================== Close ====================

void StoredFieldsWriter::close() {
    documents_.clear();
    currentDocument_.clear();
    inDocument_ = false;
    finished_ = false;
    numDocs_ = 0;
    bytesUsed_ = 0;
}

// ==================== VInt/VLong/String Encoding Helpers ====================

void StoredFieldsWriter::encodeVInt(std::vector<uint8_t>& buf, int32_t i) {
    auto value = static_cast<uint32_t>(i);
    while (value > 0x7F) {
        buf.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value));
}

void StoredFieldsWriter::encodeVLong(std::vector<uint8_t>& buf, int64_t l) {
    auto value = static_cast<uint64_t>(l);
    while (value > 0x7F) {
        buf.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value));
}

void StoredFieldsWriter::encodeString(std::vector<uint8_t>& buf, const std::string& s) {
    encodeVInt(buf, static_cast<int32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ==================== Block Serialization ====================

std::vector<uint8_t> StoredFieldsWriter::serializeBlock(int startDoc, int count) {
    std::vector<uint8_t> raw;
    // Reserve a reasonable amount to avoid frequent reallocations
    raw.reserve(count * 128);

    for (int i = 0; i < count; i++) {
        const auto& doc = documents_[startDoc + i];

        // Write number of fields
        encodeVInt(raw, static_cast<int32_t>(doc.fields.size()));

        // Write each field
        for (const auto& field : doc.fields) {
            // Write field number
            encodeVInt(raw, field.fieldNumber);

            // Write field type
            raw.push_back(static_cast<uint8_t>(field.fieldType));

            // Write field value based on type
            switch (field.fieldType) {
                case FieldType::STRING:
                    encodeString(raw, field.stringValue);
                    break;

                case FieldType::INT:
                    encodeVInt(raw, static_cast<int32_t>(field.numericValue));
                    break;

                case FieldType::LONG:
                    encodeVLong(raw, field.numericValue);
                    break;
            }
        }
    }

    return raw;
}

// ==================== Private Methods ====================

void StoredFieldsWriter::writeHeader(store::IndexOutput& out) {
    out.writeString(CODEC_NAME);
    out.writeVInt(VERSION);
}

std::vector<StoredFieldsWriter::BlockEntry> StoredFieldsWriter::writeData(
    store::IndexOutput& dataOut) {
    // Write header
    writeHeader(dataOut);

    std::vector<BlockEntry> blocks;
    int totalDocs = static_cast<int>(documents_.size());
    int docIdx = 0;

    while (docIdx < totalDocs) {
        int blockSize = std::min(BLOCK_SIZE, totalDocs - docIdx);

        // Record block start offset
        BlockEntry entry;
        entry.offset = dataOut.getFilePointer();
        entry.numDocsInBlock = blockSize;

        // Serialize documents in this block to raw bytes
        std::vector<uint8_t> raw = serializeBlock(docIdx, blockSize);
        int rawLength = static_cast<int>(raw.size());

#ifdef HAVE_LZ4
        // Compress with LZ4
        int maxCompressedSize = LZ4_compressBound(rawLength);
        std::vector<uint8_t> compressed(maxCompressedSize);

        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char*>(raw.data()),
            reinterpret_cast<char*>(compressed.data()),
            rawLength, maxCompressedSize);

        if (compressedSize <= 0) {
            throw std::runtime_error("LZ4 compression failed");
        }

        // Write block header
        dataOut.writeVInt(blockSize);
        dataOut.writeVInt(rawLength);
        dataOut.writeVInt(compressedSize);

        // Write compressed data
        dataOut.writeBytes(compressed.data(), compressedSize);
#else
        // No LZ4 available: store uncompressed (compressedLength == rawLength signals this)
        dataOut.writeVInt(blockSize);
        dataOut.writeVInt(rawLength);
        dataOut.writeVInt(rawLength);
        dataOut.writeBytes(raw.data(), rawLength);
#endif

        blocks.push_back(entry);
        docIdx += blockSize;
    }

    return blocks;
}

void StoredFieldsWriter::writeIndex(store::IndexOutput& indexOut,
                                    const std::vector<BlockEntry>& blocks) {
    // Write header
    writeHeader(indexOut);

    // Write total number of documents
    indexOut.writeVInt(numDocs_);

    // Write number of blocks
    indexOut.writeVInt(static_cast<int>(blocks.size()));

    // Write each block entry
    for (const auto& block : blocks) {
        indexOut.writeVLong(block.offset);
        indexOut.writeVInt(block.numDocsInBlock);
    }
}

}  // namespace codecs
}  // namespace diagon
