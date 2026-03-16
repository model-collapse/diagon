// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SortedNumericDocValuesReader.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

static const std::string SNDV_CODEC_NAME = "DiagonSNDV";
static const int SNDV_VERSION = 1;

// ==================== SortedNumericDocValuesReader ====================

SortedNumericDocValuesReader::SortedNumericDocValuesReader(
    std::unique_ptr<store::IndexInput> dataInput, std::unique_ptr<store::IndexInput> metaInput)
    : dataInput_(std::move(dataInput))
    , metaInput_(std::move(metaInput)) {
    readMetadata();
}

void SortedNumericDocValuesReader::readMetadata() {
    std::string codecName = metaInput_->readString();
    if (codecName != SNDV_CODEC_NAME) {
        throw std::runtime_error("Invalid codec name: " + codecName + " (expected " +
                                 SNDV_CODEC_NAME + ")");
    }

    int version = metaInput_->readVInt();
    if (version != SNDV_VERSION) {
        throw std::runtime_error("Invalid version: " + std::to_string(version) + " (expected " +
                                 std::to_string(SNDV_VERSION) + ")");
    }

    int numFields = metaInput_->readVInt();

    for (int i = 0; i < numFields; i++) {
        FieldMetadata meta;
        meta.fieldNumber = metaInput_->readVInt();
        meta.fieldName = metaInput_->readString();
        meta.numDocs = metaInput_->readVInt();
        meta.numValues = metaInput_->readVInt();
        meta.totalValueCount = metaInput_->readVLong();
        meta.dataOffset = metaInput_->readVLong();
        meta.dataLength = metaInput_->readVLong();

        fieldsByName_[meta.fieldName] = meta;
        fieldsByNumber_[meta.fieldNumber] = meta;
    }

    // Read sentinel
    [[maybe_unused]] int sentinel = metaInput_->readVInt();
}

std::unique_ptr<index::SortedNumericDocValues>
SortedNumericDocValuesReader::getSortedNumeric(const std::string& fieldName) {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemorySortedNumericDocValues>(std::move(data.values));
}

std::unique_ptr<index::SortedNumericDocValues>
SortedNumericDocValuesReader::getSortedNumeric(int32_t fieldNumber) {
    auto it = fieldsByNumber_.find(fieldNumber);
    if (it == fieldsByNumber_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemorySortedNumericDocValues>(std::move(data.values));
}

bool SortedNumericDocValuesReader::hasField(const std::string& fieldName) const {
    return fieldsByName_.find(fieldName) != fieldsByName_.end();
}

bool SortedNumericDocValuesReader::hasField(int32_t fieldNumber) const {
    return fieldsByNumber_.find(fieldNumber) != fieldsByNumber_.end();
}

const SortedNumericDocValuesReader::FieldMetadata*
SortedNumericDocValuesReader::getFieldMetadata(const std::string& fieldName) const {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }
    return &it->second;
}

SortedNumericDocValuesReader::FieldData
SortedNumericDocValuesReader::loadFieldData(const FieldMetadata& meta) {
    dataInput_->seek(meta.dataOffset);

    FieldData data;
    data.values.resize(meta.numDocs);

    // Read counts array
    std::vector<int> counts(meta.numDocs);
    for (int docID = 0; docID < meta.numDocs; docID++) {
        counts[docID] = dataInput_->readVInt();
    }

    // Read values array: for each doc, read count values
    for (int docID = 0; docID < meta.numDocs; docID++) {
        int count = counts[docID];
        if (count > 0) {
            data.values[docID].reserve(count);
            for (int j = 0; j < count; j++) {
                data.values[docID].push_back(dataInput_->readLong());
            }
        }
    }

    return data;
}

// ==================== MemorySortedNumericDocValues ====================

MemorySortedNumericDocValues::MemorySortedNumericDocValues(std::vector<std::vector<int64_t>> values)
    : values_(std::move(values))
    , maxDoc_(static_cast<int>(values_.size()))
    , cost_(0) {
    for (const auto& v : values_) {
        if (!v.empty()) {
            cost_++;
        }
    }
}

int MemorySortedNumericDocValues::nextDoc() {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    docID_++;
    while (docID_ < maxDoc_ && values_[docID_].empty()) {
        docID_++;
    }

    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    } else {
        valueIndex_ = 0;
    }
    return docID_;
}

int MemorySortedNumericDocValues::advance(int target) {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    if (target < docID_) {
        throw std::runtime_error("Cannot advance backwards");
    }

    docID_ = target;
    while (docID_ < maxDoc_ && values_[docID_].empty()) {
        docID_++;
    }

    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    } else {
        valueIndex_ = 0;
    }
    return docID_;
}

bool MemorySortedNumericDocValues::advanceExact(int target) {
    if (target < 0 || target >= maxDoc_) {
        return false;
    }

    docID_ = target;
    valueIndex_ = 0;
    return !values_[target].empty();
}

int64_t MemorySortedNumericDocValues::nextValue() {
    if (docID_ < 0 || docID_ >= maxDoc_ || values_[docID_].empty()) {
        throw std::runtime_error("No values at current doc: " + std::to_string(docID_));
    }
    if (valueIndex_ >= static_cast<int>(values_[docID_].size())) {
        throw std::runtime_error("All values consumed for doc: " + std::to_string(docID_));
    }
    return values_[docID_][valueIndex_++];
}

int MemorySortedNumericDocValues::docValueCount() const {
    if (docID_ < 0 || docID_ >= maxDoc_) {
        return 0;
    }
    return static_cast<int>(values_[docID_].size());
}

}  // namespace codecs
}  // namespace diagon
