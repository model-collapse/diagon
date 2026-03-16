// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/BinaryDocValuesWriter.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

// Codec constants
static const std::string BDV_CODEC_NAME = "DiagonBDV";
static const int BDV_VERSION = 1;

// ==================== Constructor ====================

BinaryDocValuesWriter::BinaryDocValuesWriter(const std::string& segmentName, int maxDoc)
    : segmentName_(segmentName)
    , maxDoc_(maxDoc) {}

// ==================== Add Value (bytes) ====================

void BinaryDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                     const uint8_t* bytes, int length) {
    if (docID < 0 || docID >= maxDoc_) {
        throw std::invalid_argument("docID out of range: " + std::to_string(docID));
    }

    FieldBuffer* buffer = getOrCreateBuffer(fieldInfo);

    if (buffer->finished) {
        throw std::runtime_error("Field already finished: " + fieldInfo.name);
    }

    // Ensure vectors are sized
    if (buffer->values.empty()) {
        buffer->values.resize(maxDoc_);
        buffer->docsWithField.resize(maxDoc_, false);
    }

    // Check for duplicate
    if (buffer->docsWithField[docID]) {
        throw std::invalid_argument("DocValuesField \"" + fieldInfo.name +
                                    "\" appears more than once for docID " + std::to_string(docID));
    }

    // Store value
    if (length > 0) {
        buffer->values[docID].assign(bytes, bytes + length);
    }
    // length == 0 means empty byte array (distinct from missing)
    buffer->docsWithField[docID] = true;
    buffer->numValues++;
}

// ==================== Add Value (string) ====================

void BinaryDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                     const std::string& value) {
    addValue(fieldInfo, docID, reinterpret_cast<const uint8_t*>(value.data()),
             static_cast<int>(value.size()));
}

// ==================== Finish Field ====================

void BinaryDocValuesWriter::finishField(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it == fieldBuffers_.end()) {
        // No values for this field - that's OK
        return;
    }

    it->second->finished = true;
}

// ==================== Flush ====================

void BinaryDocValuesWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut) {
    // Write header to data file
    dataOut.writeString(BDV_CODEC_NAME);
    dataOut.writeVInt(BDV_VERSION);

    // Write each field's data
    fieldMetadata_.clear();
    fieldMetadata_.reserve(fieldBuffers_.size());

    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        if (!buffer->finished) {
            throw std::runtime_error("Field not finished: " + buffer->fieldName);
        }

        // Write field data to .bdvd and collect metadata
        FieldMetadata meta = writeFieldData(dataOut, *buffer);
        fieldMetadata_.push_back(meta);
    }

    // Write metadata file
    writeMetadata(metaOut);
}

// ==================== RAM Usage ====================

int64_t BinaryDocValuesWriter::ramBytesUsed() const {
    int64_t bytes = 0;
    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        // Per-doc vectors: vector overhead + content bytes
        for (const auto& v : buffer->values) {
            bytes += static_cast<int64_t>(sizeof(std::vector<uint8_t>) + v.size());
        }
        // docsWithField bitmap: 1 byte per doc (simplified)
        bytes += static_cast<int64_t>(buffer->docsWithField.size());
    }
    return bytes;
}

// ==================== Private Methods ====================

BinaryDocValuesWriter::FieldBuffer*
BinaryDocValuesWriter::getOrCreateBuffer(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it != fieldBuffers_.end()) {
        return it->second.get();
    }

    // Create new buffer
    auto buffer = std::make_unique<FieldBuffer>();
    buffer->fieldName = fieldInfo.name;
    buffer->fieldNumber = fieldInfo.number;
    auto* ptr = buffer.get();
    fieldBuffers_[fieldInfo.number] = std::move(buffer);
    return ptr;
}

BinaryDocValuesWriter::FieldMetadata
BinaryDocValuesWriter::writeFieldData(store::IndexOutput& dataOut, const FieldBuffer& buffer) {
    FieldMetadata meta;
    meta.fieldName = buffer.fieldName;
    meta.fieldNumber = buffer.fieldNumber;
    meta.numDocs = maxDoc_;
    meta.numValues = buffer.numValues;
    meta.dataOffset = dataOut.getFilePointer();

    // Write per-doc binary data: vInt(length) + bytes
    for (int docID = 0; docID < maxDoc_; docID++) {
        const auto& val = buffer.values[docID];
        dataOut.writeVInt(static_cast<int32_t>(val.size()));
        if (!val.empty()) {
            dataOut.writeBytes(val.data(), val.size());
        }
    }

    // Write hasValue bitmap: (numDocs+7)/8 bytes, bit i = docsWithField[i]
    int bitmapBytes = (maxDoc_ + 7) / 8;
    for (int byteIdx = 0; byteIdx < bitmapBytes; byteIdx++) {
        uint8_t b = 0;
        for (int bit = 0; bit < 8; bit++) {
            int docID = byteIdx * 8 + bit;
            if (docID < maxDoc_ && buffer.docsWithField[docID]) {
                b |= static_cast<uint8_t>(1 << bit);
            }
        }
        dataOut.writeByte(b);
    }

    meta.dataLength = dataOut.getFilePointer() - meta.dataOffset;
    return meta;
}

void BinaryDocValuesWriter::writeMetadata(store::IndexOutput& metaOut) {
    // Write header
    metaOut.writeString(BDV_CODEC_NAME);
    metaOut.writeVInt(BDV_VERSION);

    // Write number of fields
    metaOut.writeVInt(static_cast<int>(fieldMetadata_.size()));

    // Write each field's metadata
    for (const auto& meta : fieldMetadata_) {
        metaOut.writeVInt(meta.fieldNumber);
        metaOut.writeString(meta.fieldName);
        metaOut.writeVInt(meta.numDocs);
        metaOut.writeVInt(meta.numValues);
        metaOut.writeVLong(meta.dataOffset);
        metaOut.writeVLong(meta.dataLength);
    }

    // Write sentinel
    metaOut.writeVInt(-1);
}

}  // namespace codecs
}  // namespace diagon
