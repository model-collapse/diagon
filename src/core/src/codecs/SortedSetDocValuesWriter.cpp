// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SortedSetDocValuesWriter.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {

static const std::string SSDV_CODEC_NAME = "DiagonSSDV";
static const int SSDV_VERSION = 1;

// ==================== Constructor ====================

SortedSetDocValuesWriter::SortedSetDocValuesWriter(const std::string& segmentName, int maxDoc)
    : segmentName_(segmentName)
    , maxDoc_(maxDoc) {}

// ==================== Add Value ====================

void SortedSetDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                        const std::string& value) {
    if (docID < 0 || docID >= maxDoc_) {
        throw std::invalid_argument("docID out of range: " + std::to_string(docID));
    }

    FieldBuffer* buffer = getOrCreateBuffer(fieldInfo);

    if (buffer->finished) {
        throw std::runtime_error("Field already finished: " + fieldInfo.name);
    }

    // Lazy resize values vector to maxDoc_
    if (buffer->values.empty()) {
        buffer->values.resize(maxDoc_);
    }

    // Track first value for this doc
    bool wasEmpty = buffer->values[docID].empty();

    // Insert into set (auto-deduplicates and sorts)
    buffer->values[docID].insert(value);

    // Increment numValues only on first value for this doc
    if (wasEmpty) {
        buffer->numValues++;
    }
}

void SortedSetDocValuesWriter::addValue(const index::FieldInfo& fieldInfo, int docID,
                                        const uint8_t* bytes, int length) {
    addValue(fieldInfo, docID,
             std::string(reinterpret_cast<const char*>(bytes), static_cast<size_t>(length)));
}

// ==================== Finish Field ====================

void SortedSetDocValuesWriter::finishField(const index::FieldInfo& fieldInfo) {
    auto it = fieldBuffers_.find(fieldInfo.number);
    if (it == fieldBuffers_.end()) {
        return;
    }
    it->second->finished = true;
}

// ==================== Flush ====================

void SortedSetDocValuesWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& metaOut) {
    // Write header to data file
    dataOut.writeString(SSDV_CODEC_NAME);
    dataOut.writeVInt(SSDV_VERSION);

    // Write each field's data
    fieldMetadata_.clear();
    fieldMetadata_.reserve(fieldBuffers_.size());

    for (auto& [fieldNum, buffer] : fieldBuffers_) {
        if (!buffer->finished) {
            throw std::runtime_error("Field not finished: " + buffer->fieldName);
        }

        FieldMetadata meta = writeFieldData(dataOut, *buffer);
        fieldMetadata_.push_back(meta);
    }

    // Write metadata file
    writeMetadata(metaOut);
}

// ==================== RAM Usage ====================

int64_t SortedSetDocValuesWriter::ramBytesUsed() const {
    int64_t bytes = 0;
    for (const auto& [fieldNum, buffer] : fieldBuffers_) {
        // Outer vector: pointer per doc
        bytes += static_cast<int64_t>(buffer->values.size()) *
                 static_cast<int64_t>(sizeof(std::set<std::string>));
        // Estimate per-value string storage
        for (const auto& docValues : buffer->values) {
            for (const auto& val : docValues) {
                // std::set node overhead (~48 bytes) + string data
                bytes += 48 + static_cast<int64_t>(val.size());
            }
        }
    }
    return bytes;
}

// ==================== Private Methods ====================

SortedSetDocValuesWriter::FieldBuffer*
SortedSetDocValuesWriter::getOrCreateBuffer(const index::FieldInfo& fieldInfo) {
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

SortedSetDocValuesWriter::FieldMetadata
SortedSetDocValuesWriter::writeFieldData(store::IndexOutput& dataOut, FieldBuffer& buffer) {
    FieldMetadata meta;
    meta.fieldName = buffer.fieldName;
    meta.fieldNumber = buffer.fieldNumber;
    meta.numDocs = maxDoc_;
    meta.numValues = buffer.numValues;

    // 1. Collect all unique values across all docs into a global sorted set
    std::set<std::string> globalTerms;
    for (const auto& docValues : buffer.values) {
        for (const auto& val : docValues) {
            globalTerms.insert(val);
        }
    }

    // 2. Build ordinal map: term → ordinal
    std::unordered_map<std::string, int64_t> ordinalMap;
    ordinalMap.reserve(globalTerms.size());
    int64_t ord = 0;
    std::vector<std::string> sortedTerms;
    sortedTerms.reserve(globalTerms.size());
    for (const auto& term : globalTerms) {
        ordinalMap[term] = ord++;
        sortedTerms.push_back(term);
    }

    meta.valueCount = static_cast<int64_t>(sortedTerms.size());

    // 3. Write term dictionary
    meta.dictOffset = dataOut.getFilePointer();
    for (const auto& term : sortedTerms) {
        dataOut.writeVInt(static_cast<int32_t>(term.size()));
        if (!term.empty()) {
            dataOut.writeBytes(reinterpret_cast<const uint8_t*>(term.data()), term.size());
        }
    }
    meta.dictLength = dataOut.getFilePointer() - meta.dictOffset;

    // 4. Write per-doc ordinal sets
    meta.dataOffset = dataOut.getFilePointer();
    int64_t totalOrds = 0;
    for (int docID = 0; docID < maxDoc_; docID++) {
        const auto& docValues = buffer.values[docID];
        auto count = static_cast<int32_t>(docValues.size());
        dataOut.writeVInt(count);
        // Write sorted ordinals for this doc
        for (const auto& val : docValues) {
            int64_t ordinal = ordinalMap[val];
            dataOut.writeLong(ordinal);
            totalOrds++;
        }
    }
    meta.dataLength = dataOut.getFilePointer() - meta.dataOffset;
    meta.totalOrdCount = totalOrds;

    return meta;
}

void SortedSetDocValuesWriter::writeMetadata(store::IndexOutput& metaOut) {
    // Write header
    metaOut.writeString(SSDV_CODEC_NAME);
    metaOut.writeVInt(SSDV_VERSION);

    // Write number of fields
    metaOut.writeVInt(static_cast<int>(fieldMetadata_.size()));

    // Write each field's metadata
    for (const auto& meta : fieldMetadata_) {
        metaOut.writeVInt(meta.fieldNumber);
        metaOut.writeString(meta.fieldName);
        metaOut.writeVInt(meta.numDocs);
        metaOut.writeVInt(meta.numValues);
        metaOut.writeVLong(meta.valueCount);
        metaOut.writeVLong(meta.totalOrdCount);
        metaOut.writeVLong(meta.dataOffset);
        metaOut.writeVLong(meta.dataLength);
        metaOut.writeVLong(meta.dictOffset);
        metaOut.writeVLong(meta.dictLength);
    }

    // Write sentinel
    metaOut.writeVInt(-1);
}

}  // namespace codecs
}  // namespace diagon
