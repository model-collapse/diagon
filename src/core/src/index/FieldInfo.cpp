// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FieldInfo.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace index {

// ==================== FieldInfo ====================

void FieldInfo::validate() const {
    // Name and number
    if (name.empty()) {
        throw std::invalid_argument("Field name cannot be empty");
    }
    if (number < 0) {
        throw std::invalid_argument("Field number must be >= 0");
    }

    // Index options constraints
    if (indexOptions == IndexOptions::NONE) {
        if (storeTermVector) {
            throw std::invalid_argument("Cannot store term vectors for non-indexed field");
        }
        if (storePayloads) {
            throw std::invalid_argument("Cannot store payloads for non-indexed field");
        }
    }

    // Payloads require positions
    if (storePayloads && indexOptions < IndexOptions::DOCS_AND_FREQS_AND_POSITIONS) {
        throw std::invalid_argument("Payloads require at least DOCS_AND_FREQS_AND_POSITIONS");
    }

    // Doc values skip index compatibility
    if (docValuesSkipIndex != DocValuesSkipIndexType::NONE) {
        if (docValuesType == DocValuesType::NONE || docValuesType == DocValuesType::BINARY) {
            throw std::invalid_argument("Skip index incompatible with NONE or BINARY doc values");
        }
    }

    // Point values consistency
    if (pointDimensionCount > 0) {
        if (pointIndexDimensionCount <= 0 || pointIndexDimensionCount > pointDimensionCount) {
            throw std::invalid_argument("Invalid pointIndexDimensionCount");
        }
        if (pointNumBytes <= 0) {
            throw std::invalid_argument("pointNumBytes must be > 0");
        }
    } else {
        if (pointIndexDimensionCount != 0 || pointNumBytes != 0) {
            throw std::invalid_argument("Point fields must be zero if pointDimensionCount=0");
        }
    }

    // Special field roles
    if (softDeletesField && isParentField) {
        throw std::invalid_argument("Field cannot be both soft-deletes and parent field");
    }
}

std::optional<std::string> FieldInfo::getAttribute(const std::string& key) const {
    auto it = attributes.find(key);
    if (it != attributes.end()) {
        return it->second;
    }
    return std::nullopt;
}

void FieldInfo::putAttribute(const std::string& key, const std::string& value) {
    attributes[key] = value;
}

// ==================== FieldInfos ====================

FieldInfos::FieldInfos(std::vector<FieldInfo> infos)
    : byNumber_(std::move(infos)) {
    buildIndex();
    computeAggregateFlags();
    validateSpecialFields();
}

const FieldInfo* FieldInfos::fieldInfo(const std::string& fieldName) const {
    auto it = byName_.find(fieldName);
    return it != byName_.end() ? it->second : nullptr;
}

const FieldInfo* FieldInfos::fieldInfo(int32_t fieldNumber) const {
    if (fieldNumber >= 0 && fieldNumber < static_cast<int32_t>(byNumber_.size())) {
        return &byNumber_[fieldNumber];
    }
    return nullptr;
}

void FieldInfos::buildIndex() {
    for (const auto& info : byNumber_) {
        if (byName_.find(info.name) != byName_.end()) {
            throw std::invalid_argument("Duplicate field name: " + info.name);
        }
        byName_[info.name] = &info;
    }
}

void FieldInfos::computeAggregateFlags() {
    for (const auto& info : byNumber_) {
        info.validate();  // Validate each field

        if (info.hasFreqs())
            hasFreq_ = true;
        if (info.hasPostings())
            hasPostings_ = true;
        if (info.hasPositions())
            hasProx_ = true;
        if (info.storePayloads)
            hasPayloads_ = true;
        if (info.hasOffsets())
            hasOffsets_ = true;
        if (info.storeTermVector)
            hasTermVectors_ = true;
        if (info.hasNorms())
            hasNorms_ = true;
        if (info.hasDocValues())
            hasDocValues_ = true;
        if (info.hasPointValues())
            hasPointValues_ = true;
    }
}

void FieldInfos::validateSpecialFields() {
    int softDeletesCount = 0;
    int parentFieldCount = 0;

    for (const auto& info : byNumber_) {
        if (info.softDeletesField) {
            softDeletesCount++;
            softDeletesField_ = info.name;
        }
        if (info.isParentField) {
            parentFieldCount++;
            parentField_ = info.name;
        }
    }

    if (softDeletesCount > 1) {
        throw std::invalid_argument("Multiple soft-deletes fields not allowed");
    }
    if (parentFieldCount > 1) {
        throw std::invalid_argument("Multiple parent fields not allowed");
    }
}

// ==================== FieldInfosBuilder ====================

int32_t FieldInfosBuilder::getOrAdd(const std::string& fieldName) {
    auto it = byName_.find(fieldName);
    if (it != byName_.end()) {
        return it->second.number;
    }

    // Allocate new field number
    int32_t fieldNumber = nextFieldNumber_++;

    FieldInfo info;
    info.name = fieldName;
    info.number = fieldNumber;
    info.indexOptions = IndexOptions::NONE;
    info.docValuesType = DocValuesType::NONE;
    info.docValuesSkipIndex = DocValuesSkipIndexType::NONE;
    info.dvGen = -1;
    info.pointDimensionCount = 0;
    info.pointIndexDimensionCount = 0;
    info.pointNumBytes = 0;
    info.softDeletesField = false;
    info.isParentField = false;
    info.storeTermVector = false;
    info.omitNorms = false;
    info.storePayloads = false;

    byName_[fieldName] = info;
    return fieldNumber;
}

FieldInfo* FieldInfosBuilder::getFieldInfo(const std::string& fieldName) {
    auto it = byName_.find(fieldName);
    if (it != byName_.end()) {
        return &it->second;
    }
    return nullptr;
}

void FieldInfosBuilder::updateIndexOptions(const std::string& fieldName,
                                           IndexOptions indexOptions) {
    // Field must exist - throw if not found
    auto it = byName_.find(fieldName);
    if (it == byName_.end()) {
        throw std::invalid_argument("Cannot update non-existent field: " + fieldName);
    }

    FieldInfo& info = it->second;

    // Can only upgrade index options, not downgrade
    if (indexOptions > info.indexOptions) {
        info.indexOptions = indexOptions;
    }
}

void FieldInfosBuilder::updateDocValuesType(const std::string& fieldName,
                                            DocValuesType docValuesType) {
    // Skip NONE
    if (docValuesType == DocValuesType::NONE) {
        return;
    }

    // Get or create field
    getOrAdd(fieldName);

    auto it = byName_.find(fieldName);
    FieldInfo& info = it->second;

    // Check for conflicts
    if (info.docValuesType != DocValuesType::NONE && info.docValuesType != docValuesType) {
        throw std::invalid_argument("Cannot change DocValuesType for field: " + fieldName);
    }

    info.docValuesType = docValuesType;
}

int32_t FieldInfosBuilder::getFieldNumber(const std::string& fieldName) const {
    auto it = byName_.find(fieldName);
    if (it == byName_.end()) {
        return -1;
    }
    return it->second.number;
}

void FieldInfosBuilder::reset() {
    byName_.clear();
    nextFieldNumber_ = 0;
}

std::unique_ptr<FieldInfos> FieldInfosBuilder::finish() {
    std::vector<FieldInfo> infos;
    infos.reserve(byName_.size());

    for (auto& [name, info] : byName_) {
        infos.push_back(std::move(info));
    }

    // Sort by field number
    std::sort(infos.begin(), infos.end(),
              [](const FieldInfo& a, const FieldInfo& b) { return a.number < b.number; });

    return std::make_unique<FieldInfos>(std::move(infos));
}

}  // namespace index
}  // namespace diagon
