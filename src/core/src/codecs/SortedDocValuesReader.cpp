// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SortedDocValuesReader.h"

#include <cstring>
#include <stdexcept>

namespace diagon {
namespace codecs {

static const char MAGIC[] = "DiagonSDV\0";
static constexpr int MAGIC_LENGTH = 10;
static constexpr int32_t VERSION = 1;

// ==================== SortedDocValuesReader ====================

SortedDocValuesReader::SortedDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                                             std::unique_ptr<store::IndexInput> metaInput)
    : dataInput_(std::move(dataInput))
    , metaInput_(std::move(metaInput)) {
    readMetadata();
}

void SortedDocValuesReader::readMetadata() {
    // Read and verify magic header
    uint8_t magic[MAGIC_LENGTH];
    metaInput_->readBytes(magic, MAGIC_LENGTH);
    if (std::memcmp(magic, MAGIC, MAGIC_LENGTH) != 0) {
        throw std::runtime_error("Invalid magic header in .sdvm file");
    }

    int32_t version = metaInput_->readInt();
    if (version != VERSION) {
        throw std::runtime_error("Invalid version: " + std::to_string(version) + " (expected " +
                                 std::to_string(VERSION) + ")");
    }

    int numFields = metaInput_->readVInt();

    for (int i = 0; i < numFields; i++) {
        FieldMetadata meta;
        meta.fieldNumber = metaInput_->readVInt();

        // Check for sentinel
        if (meta.fieldNumber < 0) {
            break;
        }

        meta.fieldName = metaInput_->readString();
        meta.numDocs = metaInput_->readVInt();
        meta.numValues = metaInput_->readVInt();
        meta.valueCount = metaInput_->readVInt();
        meta.dataOffset = metaInput_->readVLong();
        meta.dataLength = metaInput_->readVLong();
        meta.dictOffset = metaInput_->readVLong();
        meta.dictLength = metaInput_->readVLong();

        fieldsByName_[meta.fieldName] = meta;
        fieldsByNumber_[meta.fieldNumber] = meta;
    }
}

std::unique_ptr<index::SortedDocValues>
SortedDocValuesReader::getSorted(const std::string& fieldName) {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemorySortedDocValues>(std::move(data.terms), std::move(data.ordinals));
}

std::unique_ptr<index::SortedDocValues> SortedDocValuesReader::getSorted(int32_t fieldNumber) {
    auto it = fieldsByNumber_.find(fieldNumber);
    if (it == fieldsByNumber_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemorySortedDocValues>(std::move(data.terms), std::move(data.ordinals));
}

bool SortedDocValuesReader::hasField(const std::string& fieldName) const {
    return fieldsByName_.find(fieldName) != fieldsByName_.end();
}

bool SortedDocValuesReader::hasField(int32_t fieldNumber) const {
    return fieldsByNumber_.find(fieldNumber) != fieldsByNumber_.end();
}

const SortedDocValuesReader::FieldMetadata*
SortedDocValuesReader::getFieldMetadata(const std::string& fieldName) const {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }
    return &it->second;
}

SortedDocValuesReader::FieldData SortedDocValuesReader::loadFieldData(const FieldMetadata& meta) {
    FieldData data;

    // Load term dictionary
    dataInput_->seek(meta.dictOffset);
    data.terms.reserve(meta.valueCount);
    for (int32_t i = 0; i < meta.valueCount; i++) {
        int32_t termLen = dataInput_->readVInt();
        std::string term(termLen, '\0');
        if (termLen > 0) {
            dataInput_->readBytes(reinterpret_cast<uint8_t*>(term.data()), termLen);
        }
        data.terms.push_back(std::move(term));
    }

    // Load ordinal column
    dataInput_->seek(meta.dataOffset);
    data.ordinals.reserve(meta.numDocs);
    for (int32_t i = 0; i < meta.numDocs; i++) {
        data.ordinals.push_back(dataInput_->readInt());
    }

    return data;
}

// ==================== MemorySortedDocValues ====================

MemorySortedDocValues::MemorySortedDocValues(std::vector<std::string> terms,
                                             std::vector<int32_t> ordinals)
    : terms_(std::move(terms))
    , ordinals_(std::move(ordinals))
    , maxDoc_(static_cast<int>(ordinals_.size())) {}

int MemorySortedDocValues::nextDoc() {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    docID_++;
    // Skip docs without a value (ordinal == -1)
    while (docID_ < maxDoc_ && ordinals_[docID_] == -1) {
        docID_++;
    }

    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    }
    return docID_;
}

int MemorySortedDocValues::advance(int target) {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    if (target < docID_) {
        throw std::runtime_error("Cannot advance backwards");
    }

    docID_ = target;
    // Skip to next doc with a value
    while (docID_ < maxDoc_ && ordinals_[docID_] == -1) {
        docID_++;
    }

    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    }
    return docID_;
}

bool MemorySortedDocValues::advanceExact(int target) {
    if (target < 0 || target >= maxDoc_) {
        return false;
    }
    docID_ = target;
    return ordinals_[docID_] != -1;
}

int64_t MemorySortedDocValues::cost() const {
    // Count docs with values
    int64_t count = 0;
    for (int32_t ord : ordinals_) {
        if (ord != -1) {
            count++;
        }
    }
    return count;
}

int MemorySortedDocValues::ordValue() const {
    if (docID_ < 0 || docID_ >= maxDoc_) {
        throw std::runtime_error("Invalid docID: " + std::to_string(docID_));
    }
    return ordinals_[docID_];
}

util::BytesRef MemorySortedDocValues::lookupOrd(int ord) const {
    if (ord < 0 || ord >= static_cast<int>(terms_.size())) {
        throw std::out_of_range("Ordinal out of range: " + std::to_string(ord));
    }
    const std::string& term = terms_[ord];
    return util::BytesRef(reinterpret_cast<const uint8_t*>(term.data()), term.size());
}

int MemorySortedDocValues::getValueCount() const {
    return static_cast<int>(terms_.size());
}

}  // namespace codecs
}  // namespace diagon
