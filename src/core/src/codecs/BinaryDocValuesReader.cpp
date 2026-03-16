// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/BinaryDocValuesReader.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

// Codec constants (must match writer)
static const std::string BDV_CODEC_NAME = "DiagonBDV";
static const int BDV_VERSION = 1;

// ==================== BinaryDocValuesReader ====================

BinaryDocValuesReader::BinaryDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                                             std::unique_ptr<store::IndexInput> metaInput)
    : dataInput_(std::move(dataInput))
    , metaInput_(std::move(metaInput)) {
    readMetadata();
}

void BinaryDocValuesReader::readMetadata() {
    // Read header
    std::string codecName = metaInput_->readString();
    if (codecName != BDV_CODEC_NAME) {
        throw std::runtime_error("Invalid codec name: " + codecName + " (expected " +
                                 BDV_CODEC_NAME + ")");
    }

    int version = metaInput_->readVInt();
    if (version != BDV_VERSION) {
        throw std::runtime_error("Invalid version: " + std::to_string(version) + " (expected " +
                                 std::to_string(BDV_VERSION) + ")");
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

        // Store in both maps
        fieldsByName_[meta.fieldName] = meta;
        fieldsByNumber_[meta.fieldNumber] = meta;
    }

    // Read sentinel (fieldNumber = -1)
    [[maybe_unused]] int sentinel = metaInput_->readVInt();
}

std::unique_ptr<index::BinaryDocValues>
BinaryDocValuesReader::getBinary(const std::string& fieldName) {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemoryBinaryDocValues>(std::move(data.values),
                                                   std::move(data.docsWithField));
}

std::unique_ptr<index::BinaryDocValues> BinaryDocValuesReader::getBinary(int32_t fieldNumber) {
    auto it = fieldsByNumber_.find(fieldNumber);
    if (it == fieldsByNumber_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemoryBinaryDocValues>(std::move(data.values),
                                                   std::move(data.docsWithField));
}

bool BinaryDocValuesReader::hasField(const std::string& fieldName) const {
    return fieldsByName_.find(fieldName) != fieldsByName_.end();
}

bool BinaryDocValuesReader::hasField(int32_t fieldNumber) const {
    return fieldsByNumber_.find(fieldNumber) != fieldsByNumber_.end();
}

const BinaryDocValuesReader::FieldMetadata*
BinaryDocValuesReader::getFieldMetadata(const std::string& fieldName) const {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }
    return &it->second;
}

BinaryDocValuesReader::FieldData BinaryDocValuesReader::loadFieldData(const FieldMetadata& meta) {
    // Seek to field data in .bdvd file
    dataInput_->seek(meta.dataOffset);

    FieldData data;
    data.values.resize(meta.numDocs);

    // Read per-doc binary data: vInt(length) + bytes
    for (int docID = 0; docID < meta.numDocs; docID++) {
        int length = dataInput_->readVInt();
        if (length > 0) {
            data.values[docID].resize(length);
            dataInput_->readBytes(data.values[docID].data(), length);
        }
    }

    // Read hasValue bitmap: (numDocs+7)/8 bytes
    int bitmapBytes = (meta.numDocs + 7) / 8;
    data.docsWithField.resize(meta.numDocs, false);

    for (int byteIdx = 0; byteIdx < bitmapBytes; byteIdx++) {
        uint8_t b = dataInput_->readByte();
        for (int bit = 0; bit < 8; bit++) {
            int docID = byteIdx * 8 + bit;
            if (docID < meta.numDocs && (b & (1 << bit)) != 0) {
                data.docsWithField[docID] = true;
            }
        }
    }

    return data;
}

// ==================== MemoryBinaryDocValues ====================

MemoryBinaryDocValues::MemoryBinaryDocValues(std::vector<std::vector<uint8_t>> values,
                                             std::vector<bool> docsWithField)
    : values_(std::move(values))
    , docsWithField_(std::move(docsWithField))
    , maxDoc_(static_cast<int>(values_.size()))
    , numValues_(0) {
    for (int i = 0; i < maxDoc_; i++) {
        if (docsWithField_[i]) {
            numValues_++;
        }
    }
}

int MemoryBinaryDocValues::nextDoc() {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    docID_++;
    // Skip to next doc that has a value
    while (docID_ < maxDoc_ && !docsWithField_[docID_]) {
        docID_++;
    }
    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    }
    return docID_;
}

int MemoryBinaryDocValues::advance(int target) {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    if (target < docID_) {
        throw std::runtime_error("Cannot advance backwards");
    }

    docID_ = target;
    // Find next doc >= target that has a value
    while (docID_ < maxDoc_ && !docsWithField_[docID_]) {
        docID_++;
    }
    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    }
    return docID_;
}

bool MemoryBinaryDocValues::advanceExact(int target) {
    if (target < 0 || target >= maxDoc_) {
        return false;
    }

    docID_ = target;
    return docsWithField_[target];
}

util::BytesRef MemoryBinaryDocValues::binaryValue() const {
    if (docID_ < 0 || docID_ >= maxDoc_) {
        throw std::runtime_error("Invalid docID: " + std::to_string(docID_));
    }
    const auto& val = values_[docID_];
    return util::BytesRef(val.data(), val.size());
}

}  // namespace codecs
}  // namespace diagon
