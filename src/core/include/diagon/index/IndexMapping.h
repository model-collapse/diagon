// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace index {

/**
 * Element type for array fields
 */
enum class ArrayElementType : uint8_t {
    TEXT,     // TextField - tokenized, full-text search
    STRING,   // StringField - exact match, not tokenized
    NUMERIC   // NumericDocValuesField - numeric values
};

/**
 * IndexMapping - Schema definition for an index
 *
 * Defines field types and array configurations.
 * Users must explicitly declare array fields using addArrayField().
 *
 * Based on: Elasticsearch index mapping / ClickHouse table schema
 */
class IndexMapping {
public:
    IndexMapping() = default;

    /**
     * Add single-valued field
     *
     * @param name Field name
     * @param indexOptions What to index (DOCS, DOCS_AND_FREQS, etc.)
     * @param docValuesType Column storage type (NONE, NUMERIC, SORTED, etc.)
     * @param stored Store original value
     * @param tokenized Apply tokenization
     * @param omitNorms Omit length normalization
     */
    void addField(const std::string& name,
                  IndexOptions indexOptions,
                  DocValuesType docValuesType = DocValuesType::NONE,
                  bool stored = false,
                  bool tokenized = false,
                  bool omitNorms = false);

    /**
     * Add multi-valued (array) field
     *
     * @param name Field name
     * @param elementType Type of array elements (TEXT, STRING, NUMERIC)
     * @param stored Store original values
     */
    void addArrayField(const std::string& name, ArrayElementType elementType, bool stored = false);

    /**
     * Check if field is declared as multi-valued
     *
     * @param name Field name
     * @return true if field is an array
     */
    bool isMultiValued(const std::string& name) const;

    /**
     * Get element type for array field
     *
     * @param name Field name
     * @return element type, or nullopt if not an array field
     */
    std::optional<ArrayElementType> getElementType(const std::string& name) const;

    /**
     * Get field info
     *
     * @param name Field name
     * @return field info, or nullptr if not found
     */
    const FieldInfo* getFieldInfo(const std::string& name) const;

    /**
     * Check if field exists in mapping
     */
    bool hasField(const std::string& name) const;

    /**
     * Get all field names
     */
    std::vector<std::string> fieldNames() const;

    /**
     * Number of fields in mapping
     */
    size_t size() const { return fields_.size(); }

private:
    struct FieldMapping {
        FieldInfo info;
        std::optional<ArrayElementType> elementType;  // Set if multi-valued
    };

    std::unordered_map<std::string, FieldMapping> fields_;
};

}  // namespace index
}  // namespace diagon
