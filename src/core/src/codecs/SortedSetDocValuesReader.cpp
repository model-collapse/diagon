// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SortedSetDocValuesReader.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

static const std::string SSDV_CODEC_NAME = "DiagonSSDV";
static const int SSDV_VERSION = 1;

// ==================== SortedSetDocValuesReader ====================

SortedSetDocValuesReader::SortedSetDocValuesReader(std::unique_ptr<store::IndexInput> dataInput,
                                                   std::unique_ptr<store::IndexInput> metaInput)
    : dataInput_(std::move(dataInput))
    , metaInput_(std::move(metaInput)) {
    readMetadata();
}

void SortedSetDocValuesReader::readMetadata() {
    // Read header
    std::string codecName = metaInput_->readString();
    if (codecName != SSDV_CODEC_NAME) {
        throw std::runtime_error("Invalid codec name: " + codecName + " (expected " +
                                 SSDV_CODEC_NAME + ")");
    }

    int version = metaInput_->readVInt();
    if (version != SSDV_VERSION) {
        throw std::runtime_error("Invalid version: " + std::to_string(version) + " (expected " +
                                 std::to_string(SSDV_VERSION) + ")");
    }

    // Read number of fields
    int numFields = metaInput_->readVInt();

    // Read each field's metadata
    for (int i = 0; i < numFields; i++) {
        FieldMetadata meta;
        meta.fieldNumber = metaInput_->readVInt();

        // Check sentinel
        if (meta.fieldNumber == -1) {
            break;
        }

        meta.fieldName = metaInput_->readString();
        meta.numDocs = metaInput_->readVInt();
        meta.numValues = metaInput_->readVInt();
        meta.valueCount = metaInput_->readVLong();
        meta.totalOrdCount = metaInput_->readVLong();
        meta.dataOffset = metaInput_->readVLong();
        meta.dataLength = metaInput_->readVLong();
        meta.dictOffset = metaInput_->readVLong();
        meta.dictLength = metaInput_->readVLong();

        fieldsByName_[meta.fieldName] = meta;
        fieldsByNumber_[meta.fieldNumber] = meta;
    }
}

std::unique_ptr<index::SortedSetDocValues>
SortedSetDocValuesReader::getSortedSet(const std::string& fieldName) {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemorySortedSetDocValues>(std::move(data.terms),
                                                      std::move(data.docOrdinals));
}

std::unique_ptr<index::SortedSetDocValues>
SortedSetDocValuesReader::getSortedSet(int32_t fieldNumber) {
    auto it = fieldsByNumber_.find(fieldNumber);
    if (it == fieldsByNumber_.end()) {
        return nullptr;
    }

    FieldData data = loadFieldData(it->second);
    return std::make_unique<MemorySortedSetDocValues>(std::move(data.terms),
                                                      std::move(data.docOrdinals));
}

bool SortedSetDocValuesReader::hasField(const std::string& fieldName) const {
    return fieldsByName_.find(fieldName) != fieldsByName_.end();
}

bool SortedSetDocValuesReader::hasField(int32_t fieldNumber) const {
    return fieldsByNumber_.find(fieldNumber) != fieldsByNumber_.end();
}

const SortedSetDocValuesReader::FieldMetadata*
SortedSetDocValuesReader::getFieldMetadata(const std::string& fieldName) const {
    auto it = fieldsByName_.find(fieldName);
    if (it == fieldsByName_.end()) {
        return nullptr;
    }
    return &it->second;
}

SortedSetDocValuesReader::FieldData
SortedSetDocValuesReader::loadFieldData(const FieldMetadata& meta) {
    FieldData result;

    // 1. Load term dictionary
    dataInput_->seek(meta.dictOffset);
    result.terms.reserve(static_cast<size_t>(meta.valueCount));
    for (int64_t i = 0; i < meta.valueCount; i++) {
        int32_t termLength = dataInput_->readVInt();
        std::string term(static_cast<size_t>(termLength), '\0');
        if (termLength > 0) {
            dataInput_->readBytes(reinterpret_cast<uint8_t*>(term.data()),
                                  static_cast<size_t>(termLength));
        }
        result.terms.push_back(std::move(term));
    }

    // 2. Load per-doc ordinal sets
    dataInput_->seek(meta.dataOffset);
    result.docOrdinals.resize(static_cast<size_t>(meta.numDocs));
    for (int docID = 0; docID < meta.numDocs; docID++) {
        int32_t count = dataInput_->readVInt();
        if (count > 0) {
            result.docOrdinals[docID].reserve(static_cast<size_t>(count));
            for (int32_t j = 0; j < count; j++) {
                int64_t ordinal = dataInput_->readLong();
                result.docOrdinals[docID].push_back(ordinal);
            }
        }
    }

    return result;
}

// ==================== MemorySortedSetDocValues ====================

MemorySortedSetDocValues::MemorySortedSetDocValues(std::vector<std::string> terms,
                                                   std::vector<std::vector<int64_t>> docOrdinals)
    : terms_(std::move(terms))
    , docOrdinals_(std::move(docOrdinals))
    , maxDoc_(static_cast<int>(docOrdinals_.size())) {
    // Compute cost: count of docs with at least one value
    for (const auto& ords : docOrdinals_) {
        if (!ords.empty()) {
            cost_++;
        }
    }
}

int MemorySortedSetDocValues::nextDoc() {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    docID_++;
    // Find next doc with ordinals
    while (docID_ < maxDoc_ && docOrdinals_[docID_].empty()) {
        docID_++;
    }

    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    } else {
        ordIndex_ = 0;
    }
    return docID_;
}

int MemorySortedSetDocValues::advance(int target) {
    if (docID_ == search::DocIdSetIterator::NO_MORE_DOCS) {
        return docID_;
    }

    if (target < docID_) {
        throw std::runtime_error("Cannot advance backwards");
    }

    docID_ = target;
    // Find next doc with ordinals from target
    while (docID_ < maxDoc_ && docOrdinals_[docID_].empty()) {
        docID_++;
    }

    if (docID_ >= maxDoc_) {
        docID_ = search::DocIdSetIterator::NO_MORE_DOCS;
    } else {
        ordIndex_ = 0;
    }
    return docID_;
}

bool MemorySortedSetDocValues::advanceExact(int target) {
    if (target < 0 || target >= maxDoc_) {
        return false;
    }

    docID_ = target;
    ordIndex_ = 0;
    return !docOrdinals_[target].empty();
}

int64_t MemorySortedSetDocValues::nextOrd() {
    if (docID_ < 0 || docID_ >= maxDoc_) {
        return NO_MORE_ORDS;
    }

    const auto& ords = docOrdinals_[docID_];
    if (ordIndex_ < static_cast<int>(ords.size())) {
        return ords[ordIndex_++];
    }
    return NO_MORE_ORDS;
}

util::BytesRef MemorySortedSetDocValues::lookupOrd(int64_t ord) const {
    if (ord < 0 || ord >= static_cast<int64_t>(terms_.size())) {
        throw std::invalid_argument("Ordinal out of range: " + std::to_string(ord));
    }
    const auto& term = terms_[static_cast<size_t>(ord)];
    return util::BytesRef(reinterpret_cast<const uint8_t*>(term.data()), term.size());
}

int64_t MemorySortedSetDocValues::getValueCount() const {
    return static_cast<int64_t>(terms_.size());
}

}  // namespace codecs
}  // namespace diagon
