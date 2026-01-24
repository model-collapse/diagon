// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "diagon/util/BytesRef.h"
#include "diagon/index/FieldInfo.h"  // Use canonical enum definitions

namespace diagon {
namespace document {

/**
 * FieldType - Configuration for a field
 *
 * Based on: org.apache.lucene.document.FieldType
 *
 * Note: Uses index::IndexOptions and index::DocValuesType enums
 * to avoid duplication with the codec system.
 */
struct FieldType {
    index::IndexOptions indexOptions = index::IndexOptions::NONE;
    index::DocValuesType docValuesType = index::DocValuesType::NONE;
    bool stored = false;      // Store original value
    bool tokenized = false;   // Apply analysis/tokenization
    bool omitNorms = false;   // Omit length normalization

    FieldType() = default;

    // Helper constructors
    static FieldType notIndexed() {
        return FieldType{};
    }

    static FieldType storedOnly() {
        FieldType ft;
        ft.stored = true;
        return ft;
    }
};

/**
 * IndexableField - Interface for fields that can be indexed
 *
 * Based on: org.apache.lucene.index.IndexableField
 */
class IndexableField {
public:
    virtual ~IndexableField() = default;

    /** Field name */
    virtual std::string name() const = 0;

    /** Field type configuration */
    virtual const FieldType& fieldType() const = 0;

    /** String value (if field is string-valued) */
    virtual std::optional<std::string> stringValue() const = 0;

    /** Numeric value (if field is numeric-valued) */
    virtual std::optional<int64_t> numericValue() const = 0;

    /** Binary value (if field is binary-valued) */
    virtual std::optional<util::BytesRef> binaryValue() const = 0;

    /**
     * Tokenize the field value.
     * For Phase 2: simple whitespace tokenization
     *
     * @return vector of tokens
     */
    virtual std::vector<std::string> tokenize() const = 0;
};

}  // namespace document
}  // namespace diagon
