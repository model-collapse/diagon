// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/NumericDocValuesWriter.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {

// Codec constants
static const std::string CODEC_NAME = "DiagonDocValues";
static const int VERSION = 1;
static const std::string DATA_EXTENSION = "dvd";
static const std::string META_EXTENSION = "dvm";

// ==================== Constructor ====================

NumericDocValuesWriter::NumericDocValuesWriter(const std::string& segmentName, int maxDoc)
    : segmentName_(segmentName)
    , maxDoc_(maxDoc) {}

// ==================== Add Value ====================

void NumericDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID, int64_t value) {
    if (docID < 0 || docID >= maxDoc_) {
        throw std::invalid_argument("docID out of range: " + std::to_string(docID));
    }

    // Get or create buffer for this field
    FieldBuffer* buffer = getOrCreateBuffer(fieldInfo);

    if (buffer->finished) {
        throw std::runtime_error("Field already finished: " + fieldInfo.name);
    }

    // Ensure vectors are sized
    if (buffer->values.empty()) {
        buffer->values.resize(maxDoc_, 0);
        buffer->docsWithField.resize(maxDoc_, false);
    }

    // Check for duplicate
    if (buffer->docsWithField[docID]) {
        throw std::invalid_argument("DocValuesField \"" + fieldInfo.name +
                                    "\" appears more than once for docID " + std::to_string(docID));
    }

    // Store value
    buffer->values[docID] = value;
    buffer->docsWithField[docID] = true;
    buffer->numValues++;

    // Track min/max for future compression
    if (buffer->numValues == 1) {
        buffer->minValue = value;
        buffer->maxValue = value;
    } else {
        buffer->minValue = std::min(buffer->minValue, value);
        buffer->maxValue = std::max(buffer->maxValue, value);
    }
}

// ==================== Finish Field ====================

void NumericDocValuesWriter::finishField(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it == fieldBuffers_.end()) {
        // No values for this field - that's OK
        return;
    }

    it->second->finished = true;
}

// ==================== Flush ====================

void NumericDocValuesWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut) {
    // Write header to data file
    dataOut.writeString(CODEC_NAME);
    dataOut.writeVInt(VERSION);

    // Write each field's data
    fieldMetadata_.clear();
    fieldMetadata_.reserve(fieldBuffers_.size());

    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        if (!buffer->finished) {
            throw std::runtime_error("Field not finished: " + buffer->fieldName);
        }

        // Write field data to .dvd and collect metadata
        FieldMetadata meta = writeFieldData(dataOut, *buffer);
        fieldMetadata_.push_back(meta);
    }

    // Write metadata file
    writeMetadata(metaOut);
}

// ==================== RAM Usage ====================

int64_t NumericDocValuesWriter::ramBytesUsed() const {
    int64_t bytes = 0;
    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        // values array: 8 bytes per doc
        bytes += buffer->values.size() * sizeof(int64_t);
        // docsWithField bitmap: 1 byte per doc (simplified)
        bytes += buffer->docsWithField.size();
    }
    return bytes;
}

// ==================== Private Methods ====================

NumericDocValuesWriter::FieldBuffer* NumericDocValuesWriter::getOrCreateBuffer(
    const index::FieldInfo& fieldInfo) {
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

NumericDocValuesWriter::FieldMetadata NumericDocValuesWriter::writeFieldData(
    store::IndexOutput& dataOut, const FieldBuffer& buffer) {
    FieldMetadata meta;
    meta.fieldName = buffer.fieldName;
    meta.fieldNumber = buffer.fieldNumber;
    meta.numDocs = maxDoc_;
    meta.numValues = buffer.numValues;
    meta.minValue = buffer.minValue;
    meta.maxValue = buffer.maxValue;
    meta.dataOffset = dataOut.getFilePointer();

    // Simple format: write all values densely
    // For docs without values, write 0
    // TODO: Add compression (delta, bitpacking) later
    for (int docID = 0; docID < maxDoc_; docID++) {
        int64_t value = buffer.docsWithField[docID] ? buffer.values[docID] : 0;
        dataOut.writeLong(value);
    }

    meta.dataLength = dataOut.getFilePointer() - meta.dataOffset;
    return meta;
}

void NumericDocValuesWriter::writeMetadata(store::IndexOutput& metaOut) {
    // Write header
    metaOut.writeString(CODEC_NAME);
    metaOut.writeVInt(VERSION);

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
        metaOut.writeLong(meta.minValue);
        metaOut.writeLong(meta.maxValue);
    }
}

}  // namespace codecs
}  // namespace diagon
