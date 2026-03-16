// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SortedNumericDocValuesWriter.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {

static const std::string SNDV_CODEC_NAME = "DiagonSNDV";
static const int SNDV_VERSION = 1;

// ==================== Constructor ====================

SortedNumericDocValuesWriter::SortedNumericDocValuesWriter(const std::string& segmentName,
                                                           int maxDoc)
    : segmentName_(segmentName)
    , maxDoc_(maxDoc) {}

// ==================== Add Value ====================

void SortedNumericDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                            int64_t value) {
    if (docID < 0 || docID >= maxDoc_) {
        throw std::invalid_argument("docID out of range: " + std::to_string(docID));
    }

    FieldBuffer* buffer = getOrCreateBuffer(fieldInfo);

    if (buffer->finished) {
        throw std::runtime_error("Field already finished: " + fieldInfo.name);
    }

    // Lazy resize to maxDoc_ on first access
    if (buffer->values.empty()) {
        buffer->values.resize(maxDoc_);
    }

    // Track docs with at least one value
    if (buffer->values[docID].empty()) {
        buffer->numValues++;
    }

    buffer->values[docID].push_back(value);
    buffer->totalValueCount++;
}

// ==================== Finish Field ====================

void SortedNumericDocValuesWriter::finishField(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it == fieldBuffers_.end()) {
        return;
    }

    it->second->finished = true;
}

// ==================== Flush ====================

void SortedNumericDocValuesWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut) {
    dataOut.writeString(SNDV_CODEC_NAME);
    dataOut.writeVInt(SNDV_VERSION);

    fieldMetadata_.clear();
    fieldMetadata_.reserve(fieldBuffers_.size());

    for (auto& [fieldNum, buffer] : fieldBuffers_) {
        if (!buffer->finished) {
            throw std::runtime_error("Field not finished: " + buffer->fieldName);
        }

        FieldMetadata meta = writeFieldData(dataOut, *buffer);
        fieldMetadata_.push_back(meta);
    }

    writeMetadata(metaOut);
}

// ==================== RAM Usage ====================

int64_t SortedNumericDocValuesWriter::ramBytesUsed() const {
    int64_t bytes = 0;
    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        // Outer vector overhead
        bytes += static_cast<int64_t>(buffer->values.size()) *
                 static_cast<int64_t>(sizeof(std::vector<int64_t>));
        // Inner vectors: actual value storage
        for (const auto& docValues : buffer->values) {
            bytes += static_cast<int64_t>(docValues.capacity()) *
                     static_cast<int64_t>(sizeof(int64_t));
        }
    }
    return bytes;
}

// ==================== Private Methods ====================

SortedNumericDocValuesWriter::FieldBuffer*
SortedNumericDocValuesWriter::getOrCreateBuffer(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it != fieldBuffers_.end()) {
        return it->second.get();
    }

    auto buffer = std::make_unique<FieldBuffer>();
    buffer->fieldName = fieldInfo.name;
    buffer->fieldNumber = fieldInfo.number;
    auto* ptr = buffer.get();
    fieldBuffers_[fieldInfo.number] = std::move(buffer);
    return ptr;
}

SortedNumericDocValuesWriter::FieldMetadata
SortedNumericDocValuesWriter::writeFieldData(store::IndexOutput& dataOut, FieldBuffer& buffer) {
    // Sort values within each doc (ascending)
    for (auto& docValues : buffer.values) {
        if (docValues.size() > 1) {
            std::sort(docValues.begin(), docValues.end());
        }
    }

    FieldMetadata meta;
    meta.fieldName = buffer.fieldName;
    meta.fieldNumber = buffer.fieldNumber;
    meta.numDocs = maxDoc_;
    meta.numValues = buffer.numValues;
    meta.totalValueCount = buffer.totalValueCount;
    meta.dataOffset = dataOut.getFilePointer();

    // Write counts array: one vInt per doc
    for (int docID = 0; docID < maxDoc_; docID++) {
        dataOut.writeVInt(static_cast<int>(buffer.values[docID].size()));
    }

    // Write values array: all values concatenated (sorted ascending within each doc)
    for (int docID = 0; docID < maxDoc_; docID++) {
        for (int64_t val : buffer.values[docID]) {
            dataOut.writeLong(val);
        }
    }

    meta.dataLength = dataOut.getFilePointer() - meta.dataOffset;
    return meta;
}

void SortedNumericDocValuesWriter::writeMetadata(store::IndexOutput& metaOut) {
    metaOut.writeString(SNDV_CODEC_NAME);
    metaOut.writeVInt(SNDV_VERSION);

    metaOut.writeVInt(static_cast<int>(fieldMetadata_.size()));

    for (const auto& meta : fieldMetadata_) {
        metaOut.writeVInt(meta.fieldNumber);
        metaOut.writeString(meta.fieldName);
        metaOut.writeVInt(meta.numDocs);
        metaOut.writeVInt(meta.numValues);
        metaOut.writeVLong(meta.totalValueCount);
        metaOut.writeVLong(meta.dataOffset);
        metaOut.writeVLong(meta.dataLength);
    }

    // Sentinel
    metaOut.writeVInt(-1);
}

}  // namespace codecs
}  // namespace diagon
