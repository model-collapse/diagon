// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsWriter.h"

#include <algorithm>
#include <stdexcept>

#ifdef HAVE_LZ4
#    include <lz4.h>
#endif

namespace diagon {
namespace codecs {

// Codec constants
static const std::string CODEC_NAME = "DiagonStoredFields";
static const int VERSION = 2;  // V2: LZ4 block compression

// ==================== Constructors ====================

StoredFieldsWriter::StoredFieldsWriter(const std::string& segmentName)
    : segmentName_(segmentName) {}

StoredFieldsWriter::StoredFieldsWriter(const std::string& segmentName,
                                       store::IndexOutput& dataOut,
                                       store::IndexOutput& indexOut)
    : segmentName_(segmentName)
    , dataOut_(&dataOut)
    , indexOut_(&indexOut)
    , streaming_(true) {
    blockBuffer_.reserve(BLOCK_SIZE);
}

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

    DocumentBuffer doc;
    doc.fields = std::move(currentDocument_);

    if (streaming_) {
        // Streaming mode: buffer into blockBuffer_, flush when full
        blockBuffer_.push_back(std::move(doc));
        if (static_cast<int>(blockBuffer_.size()) >= BLOCK_SIZE) {
            flushBlockToDisk();
        }
    } else {
        // Buffered mode: accumulate all documents in RAM
        documents_.push_back(std::move(doc));
    }

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

    if (streaming_) {
        // Flush remaining partial block
        if (!blockBuffer_.empty()) {
            flushBlockToDisk();
        }

        // Write index file (.fdx)
        writeIndex(*indexOut_, streamBlocks_);

        // Free streaming state
        streamBlocks_.clear();
        streamBlocks_.shrink_to_fit();
    }

    finished_ = true;
}

// ==================== Flush ====================

void StoredFieldsWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& indexOut) {
    if (!finished_) {
        throw std::runtime_error("Must call finish() before flush()");
    }

    if (streaming_) {
        // Streaming mode: data already written to disk during finishDocument()/finish()
        return;
    }

    // Buffered mode: write everything now
    std::vector<BlockEntry> blocks = writeData(dataOut);
    writeIndex(indexOut, blocks);
}

// ==================== RAM Usage ====================

int64_t StoredFieldsWriter::ramBytesUsed() const {
    return bytesUsed_;
}

// ==================== Close ====================

void StoredFieldsWriter::close() {
    documents_.clear();
    blockBuffer_.clear();
    currentDocument_.clear();
    streamBlocks_.clear();
    inDocument_ = false;
    finished_ = false;
    numDocs_ = 0;
    bytesUsed_ = 0;
    headerWritten_ = false;
    dataOut_ = nullptr;
    indexOut_ = nullptr;
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

// ==================== Serialization ====================

std::vector<uint8_t> StoredFieldsWriter::serializeDocs(const std::vector<DocumentBuffer>& docs,
                                                        int startIdx, int count) {
    std::vector<uint8_t> raw;
    raw.reserve(count * 128);

    for (int i = 0; i < count; i++) {
        const auto& doc = docs[startIdx + i];

        encodeVInt(raw, static_cast<int32_t>(doc.fields.size()));

        for (const auto& field : doc.fields) {
            encodeVInt(raw, field.fieldNumber);
            raw.push_back(static_cast<uint8_t>(field.fieldType));

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

std::vector<uint8_t> StoredFieldsWriter::serializeBlock(int startDoc, int count) {
    return serializeDocs(documents_, startDoc, count);
}

// ==================== Block Compression ====================

StoredFieldsWriter::BlockEntry
StoredFieldsWriter::writeCompressedBlock(store::IndexOutput& out,
                                          const std::vector<DocumentBuffer>& docs,
                                          int startIdx, int count) {
    BlockEntry entry;
    entry.offset = out.getFilePointer();
    entry.numDocsInBlock = count;

    std::vector<uint8_t> raw = serializeDocs(docs, startIdx, count);
    int rawLength = static_cast<int>(raw.size());

#ifdef HAVE_LZ4
    int maxCompressedSize = LZ4_compressBound(rawLength);
    std::vector<uint8_t> compressed(maxCompressedSize);

    int compressedSize = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),
                                              reinterpret_cast<char*>(compressed.data()),
                                              rawLength, maxCompressedSize);

    if (compressedSize <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }

    out.writeVInt(count);
    out.writeVInt(rawLength);
    out.writeVInt(compressedSize);
    out.writeBytes(compressed.data(), compressedSize);
#else
    out.writeVInt(count);
    out.writeVInt(rawLength);
    out.writeVInt(rawLength);
    out.writeBytes(raw.data(), rawLength);
#endif

    return entry;
}

// ==================== Streaming Mode ====================

void StoredFieldsWriter::flushBlockToDisk() {
    if (blockBuffer_.empty()) {
        return;
    }

    // Write header on first block
    if (!headerWritten_) {
        writeHeader(*dataOut_);
        headerWritten_ = true;
    }

    int count = static_cast<int>(blockBuffer_.size());
    BlockEntry entry = writeCompressedBlock(*dataOut_, blockBuffer_, 0, count);
    streamBlocks_.push_back(entry);

    // Clear block buffer — free document memory immediately
    blockBuffer_.clear();
}

// ==================== Private Methods ====================

void StoredFieldsWriter::writeHeader(store::IndexOutput& out) {
    out.writeString(CODEC_NAME);
    out.writeVInt(VERSION);
}

std::vector<StoredFieldsWriter::BlockEntry>
StoredFieldsWriter::writeData(store::IndexOutput& dataOut) {
    writeHeader(dataOut);

    std::vector<BlockEntry> blocks;
    int totalDocs = static_cast<int>(documents_.size());
    int docIdx = 0;

    while (docIdx < totalDocs) {
        int blockSize = std::min(BLOCK_SIZE, totalDocs - docIdx);

        BlockEntry entry = writeCompressedBlock(dataOut, documents_, docIdx, blockSize);
        blocks.push_back(entry);
        docIdx += blockSize;
    }

    return blocks;
}

void StoredFieldsWriter::writeIndex(store::IndexOutput& indexOut,
                                    const std::vector<BlockEntry>& blocks) {
    writeHeader(indexOut);
    indexOut.writeVInt(numDocs_);
    indexOut.writeVInt(static_cast<int>(blocks.size()));

    for (const auto& block : blocks) {
        indexOut.writeVLong(block.offset);
        indexOut.writeVInt(block.numDocsInBlock);
    }
}

}  // namespace codecs
}  // namespace diagon
