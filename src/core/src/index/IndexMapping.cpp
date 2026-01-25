// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexMapping.h"

#include <stdexcept>

namespace diagon {
namespace index {

void IndexMapping::addField(const std::string& name, IndexOptions indexOptions,
                            DocValuesType docValuesType, bool stored, bool tokenized,
                            bool omitNorms) {
    if (fields_.find(name) != fields_.end()) {
        throw std::invalid_argument("Field '" + name + "' already exists in mapping");
    }

    FieldMapping mapping;
    mapping.info.name = name;
    mapping.info.number = static_cast<int32_t>(fields_.size());
    mapping.info.indexOptions = indexOptions;
    mapping.info.docValuesType = docValuesType;
    mapping.info.omitNorms = omitNorms;
    mapping.info.multiValued = false;

    // Note: stored flag is tracked in FieldType at document level, not in schema

    fields_[name] = std::move(mapping);
}

void IndexMapping::addArrayField(const std::string& name, ArrayElementType elementType,
                                 bool stored) {
    if (fields_.find(name) != fields_.end()) {
        throw std::invalid_argument("Field '" + name + "' already exists in mapping");
    }

    FieldMapping mapping;
    mapping.info.name = name;
    mapping.info.number = static_cast<int32_t>(fields_.size());
    mapping.info.multiValued = true;
    mapping.elementType = elementType;

    // Configure based on element type
    // Note: stored flag is tracked in FieldType at document level, not in schema
    switch (elementType) {
        case ArrayElementType::TEXT:
            // ArrayTextField: tokenized, full-text search
            mapping.info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
            mapping.info.docValuesType = DocValuesType::SORTED_SET;
            mapping.info.omitNorms = false;
            break;

        case ArrayElementType::STRING:
            // ArrayStringField: exact match, not tokenized
            mapping.info.indexOptions = IndexOptions::DOCS;
            mapping.info.docValuesType = DocValuesType::SORTED_SET;
            mapping.info.omitNorms = true;
            break;

        case ArrayElementType::NUMERIC:
            // ArrayNumericField: numeric values only
            mapping.info.indexOptions = IndexOptions::NONE;
            mapping.info.docValuesType = DocValuesType::SORTED_NUMERIC;
            mapping.info.omitNorms = true;
            break;
    }

    fields_[name] = std::move(mapping);
}

bool IndexMapping::isMultiValued(const std::string& name) const {
    auto it = fields_.find(name);
    if (it == fields_.end()) {
        return false;
    }
    return it->second.info.multiValued;
}

std::optional<ArrayElementType> IndexMapping::getElementType(const std::string& name) const {
    auto it = fields_.find(name);
    if (it == fields_.end()) {
        return std::nullopt;
    }
    return it->second.elementType;
}

const FieldInfo* IndexMapping::getFieldInfo(const std::string& name) const {
    auto it = fields_.find(name);
    if (it == fields_.end()) {
        return nullptr;
    }
    return &it->second.info;
}

bool IndexMapping::hasField(const std::string& name) const {
    return fields_.find(name) != fields_.end();
}

std::vector<std::string> IndexMapping::fieldNames() const {
    std::vector<std::string> names;
    names.reserve(fields_.size());
    for (const auto& [name, _] : fields_) {
        names.push_back(name);
    }
    return names;
}

}  // namespace index
}  // namespace diagon
