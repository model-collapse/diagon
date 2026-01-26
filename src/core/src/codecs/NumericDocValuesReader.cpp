// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/NumericDocValuesReader.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {

// Codec constants (must match writer)
static const std::string CODEC_NAME = "DiagonDocValues";
static const int VERSION = 1;

// ==================== NumericDocValuesReader ====================

NumericDocValuesReader::NumericDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                                               std::unique_ptr<store::IndexInput> metaInput)
    : dataInput_(std::move(dataInput))
    , metaInput_(std::move(metaInput)) {
    readMetadata();
}

void NumericDocValuesReader::readMetadata() {
    // Read header
    std::string codecName = metaInput_->readString();
    if (codecName != CODEC_NAME) {
        throw std::runtime_error("Invalid codec name: " + codecName + " (expected " + CODEC_NAME +
                                 ")");
    }

    int version = metaInput_->readVInt();
    if (version != VERSION) {
        throw std::runtime_error("Invalid version: " + std::to_string(version) +
                                 " (expected " + std::to_string(VERSION) + ")");
    }

    // Read number of fields
    int numFields = metaInput_->readVInt();

    // Read each field's metadata
    for (int i = 0; i < numFields; i++) {
        FieldMetadata meta;
        meta.fieldNumber = metaInput_->readVInt();
        meta.fieldName = metaInput_->readString();
        meta.numDocs = metaInput_->readVInt();
        meta.numValues = metaInput_->readVInt();
        meta.dataOffset = metaInput_->readVLong();
        meta.dataLength = metaInput_->readVLong();
        meta.minValue = metaInput_->readLong();
        meta.maxValue = metaInput_->readLong();

        // Store in both maps
        fieldsByName_[meta.fieldName] = meta;
        fieldsByNumber_[meta.fieldNumber] = meta;
    }
}

std::unique_ptr<index::NumericDocValues> NumericDocValuesReader::getNumeric(
    const std::string& fieldName) {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }

    // Load values from disk
    std::vector<int64_t> values = loadValues(it->second);

    // Return iterator
    return std::make_unique<MemoryNumericDocValues>(std::move(values));
}

std::unique_ptr<index::NumericDocValues> NumericDocValuesReader::getNumeric(
    int32_t fieldNumber) {
    auto it = fieldsByNumber_.find(fieldNumber);
    if (it == fieldsByNumber_.end()) {
        return nullptr;
    }

    // Load values from disk
    std::vector<int64_t> values = loadValues(it->second);

    // Return iterator
    return std::make_unique<MemoryNumericDocValues>(std::move(values));
}

bool NumericDocValuesReader::hasField(const std::string& fieldName) const {
    return fieldsByName_.find(fieldName) != fieldsByName_.end();
}

bool NumericDocValuesReader::hasField(int32_t fieldNumber) const {
    return fieldsByNumber_.find(fieldNumber) != fieldsByNumber_.end();
}

const NumericDocValuesReader::FieldMetadata* NumericDocValuesReader::getFieldMetadata(
    const std::string& fieldName) const {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<int64_t> NumericDocValuesReader::loadValues(const FieldMetadata& meta) {
    // Seek to field data in .dvd file
    dataInput_->seek(meta.dataOffset);

    // Read dense array of int64_t values
    std::vector<int64_t> values;
    values.reserve(meta.numDocs);

    for (int docID = 0; docID < meta.numDocs; docID++) {
        int64_t value = dataInput_->readLong();
        values.push_back(value);
    }

    return values;
}

// ==================== MemoryNumericDocValues ====================

MemoryNumericDocValues::MemoryNumericDocValues(std::vector<int64_t> values)
    : values_(std::move(values))
    , maxDoc_(static_cast<int>(values_.size())) {}

int MemoryNumericDocValues::nextDoc() {
    docID_++;
    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    }
    return docID_;
}

int MemoryNumericDocValues::advance(int target) {
    if (target < docID_) {
        throw std::runtime_error("Cannot advance backwards");
    }

    docID_ = target;
    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    }
    return docID_;
}

bool MemoryNumericDocValues::advanceExact(int target) {
    if (target < 0 || target >= maxDoc_) {
        return false;
    }

    docID_ = target;
    // In our simple format, all docs are present (missing values stored as 0)
    // In a more sophisticated implementation, we'd check a docsWithField bitmap
    return true;
}

int64_t MemoryNumericDocValues::longValue() const {
    if (docID_ < 0 || docID_ >= maxDoc_) {
        throw std::runtime_error("Invalid docID: " + std::to_string(docID_));
    }
    return values_[docID_];
}

}  // namespace codecs
}  // namespace diagon
