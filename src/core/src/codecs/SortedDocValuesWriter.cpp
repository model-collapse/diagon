// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SortedDocValuesWriter.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace diagon {
namespace codecs {

static const char MAGIC[] = "DiagonSDV\0";
static constexpr int MAGIC_LENGTH = 10;
static constexpr int32_t VERSION = 1;

// ==================== Constructor ====================

SortedDocValuesWriter::SortedDocValuesWriter(const std::string& segmentName, int maxDoc)
    : segmentName_(segmentName)
    , maxDoc_(maxDoc) {}

// ==================== Add Value (string) ====================

void SortedDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                     const std::string& value) {
    if (docID < 0 || docID >= maxDoc_) {
        throw std::invalid_argument("docID out of range: " + std::to_string(docID));
    }

    FieldBuffer* buffer = getOrCreateBuffer(fieldInfo);

    if (buffer->finished) {
        throw std::runtime_error("Field already finished: " + fieldInfo.name);
    }

    if (buffer->values.empty()) {
        buffer->values.resize(maxDoc_);
        buffer->docsWithField.resize(maxDoc_, false);
    }

    if (buffer->docsWithField[docID]) {
        throw std::invalid_argument("DocValuesField \"" + fieldInfo.name +
                                    "\" appears more than once for docID " + std::to_string(docID));
    }

    buffer->values[docID] = value;
    buffer->docsWithField[docID] = true;
    buffer->numValues++;
}

// ==================== Add Value (bytes) ====================

void SortedDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                     const uint8_t* bytes, int length) {
    addValue(fieldInfo, docID, std::string(reinterpret_cast<const char*>(bytes), length));
}

// ==================== Finish Field ====================

void SortedDocValuesWriter::finishField(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it == fieldBuffers_.end()) {
        return;
    }
    it->second->finished = true;
}

// ==================== Flush ====================

void SortedDocValuesWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut) {
    fieldMetadata_.clear();
    fieldMetadata_.reserve(fieldBuffers_.size());

    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        if (!buffer->finished) {
            throw std::runtime_error("Field not finished: " + buffer->fieldName);
        }
        FieldMetadata meta = writeFieldData(dataOut, *buffer);
        fieldMetadata_.push_back(meta);
    }

    writeMetadata(metaOut);
}

// ==================== RAM Usage ====================

int64_t SortedDocValuesWriter::ramBytesUsed() const {
    int64_t bytes = 0;
    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        for (const auto& s : buffer->values) {
            bytes += static_cast<int64_t>(s.capacity() + sizeof(std::string));
        }
        bytes += static_cast<int64_t>(buffer->docsWithField.size());
    }
    return bytes;
}

// ==================== Private Methods ====================

SortedDocValuesWriter::FieldBuffer*
SortedDocValuesWriter::getOrCreateBuffer(const index::FieldInfo& fieldInfo) {
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

SortedDocValuesWriter::FieldMetadata
SortedDocValuesWriter::writeFieldData(store::IndexOutput& dataOut, const FieldBuffer& buffer) {
    // 1. Collect unique values (only from docs that have a value)
    std::set<std::string> uniqueSet;
    for (int docID = 0; docID < maxDoc_; docID++) {
        if (buffer.docsWithField[docID]) {
            uniqueSet.insert(buffer.values[docID]);
        }
    }

    // 2. Build sorted term list and ordinal map
    std::vector<std::string> sortedTerms(uniqueSet.begin(), uniqueSet.end());
    std::unordered_map<std::string, int32_t> ordinalMap;
    ordinalMap.reserve(sortedTerms.size());
    for (int32_t ord = 0; ord < static_cast<int32_t>(sortedTerms.size()); ord++) {
        ordinalMap[sortedTerms[ord]] = ord;
    }

    FieldMetadata meta;
    meta.fieldName = buffer.fieldName;
    meta.fieldNumber = buffer.fieldNumber;
    meta.numDocs = maxDoc_;
    meta.numValues = buffer.numValues;
    meta.valueCount = static_cast<int32_t>(sortedTerms.size());

    // 3. Write term dictionary
    meta.dictOffset = dataOut.getFilePointer();
    for (const auto& term : sortedTerms) {
        dataOut.writeVInt(static_cast<int32_t>(term.size()));
        if (!term.empty()) {
            dataOut.writeBytes(reinterpret_cast<const uint8_t*>(term.data()), term.size());
        }
    }
    meta.dictLength = dataOut.getFilePointer() - meta.dictOffset;

    // 4. Write ordinal column (fixed-width int32 per doc)
    meta.dataOffset = dataOut.getFilePointer();
    for (int docID = 0; docID < maxDoc_; docID++) {
        int32_t ord = -1;
        if (buffer.docsWithField[docID]) {
            auto it = ordinalMap.find(buffer.values[docID]);
            ord = it->second;
        }
        dataOut.writeInt(ord);
    }
    meta.dataLength = dataOut.getFilePointer() - meta.dataOffset;

    return meta;
}

void SortedDocValuesWriter::writeMetadata(store::IndexOutput& metaOut) {
    // Write magic header
    metaOut.writeBytes(reinterpret_cast<const uint8_t*>(MAGIC), MAGIC_LENGTH);
    metaOut.writeInt(VERSION);
    metaOut.writeVInt(static_cast<int32_t>(fieldMetadata_.size()));

    for (const auto& meta : fieldMetadata_) {
        metaOut.writeVInt(meta.fieldNumber);
        metaOut.writeString(meta.fieldName);
        metaOut.writeVInt(meta.numDocs);
        metaOut.writeVInt(meta.numValues);
        metaOut.writeVInt(meta.valueCount);
        metaOut.writeVLong(meta.dataOffset);
        metaOut.writeVLong(meta.dataLength);
        metaOut.writeVLong(meta.dictOffset);
        metaOut.writeVLong(meta.dictLength);
    }

    // Sentinel: fieldNumber = -1
    metaOut.writeVInt(-1);
}

}  // namespace codecs
}  // namespace diagon
