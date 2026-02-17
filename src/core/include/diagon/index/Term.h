// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/BytesRef.h"

#include <memory>
#include <string>

namespace diagon {
namespace index {

/**
 * Term represents a field/value pair for indexing and searching
 *
 * Based on: org.apache.lucene.index.Term
 *
 * Immutable and thread-safe.
 */
class Term {
public:
    // ==================== Construction ====================

    /**
     * Construct term from field name and text value
     */
    Term(const std::string& field, const std::string& text)
        : field_(field)
        , bytes_(std::make_shared<util::BytesRef>(text)) {}

    /**
     * Construct term from field name and bytes
     */
    Term(const std::string& field, std::shared_ptr<util::BytesRef> bytes)
        : field_(field)
        , bytes_(bytes) {}

    // ==================== Accessors ====================

    /**
     * Get field name
     */
    const std::string& field() const { return field_; }

    /**
     * Get term bytes
     */
    const util::BytesRef& bytes() const { return *bytes_; }

    /**
     * Get term as text (assumes UTF-8 encoding)
     */
    std::string text() const { return bytes_->toString(); }

    // ==================== Comparison ====================

    /**
     * Compare terms (for sorting)
     */
    int compareTo(const Term& other) const {
        // First compare field
        int fieldCmp = field_.compare(other.field_);
        if (fieldCmp != 0) {
            return fieldCmp;
        }
        // Then compare bytes
        return bytes_->compareTo(*other.bytes_);
    }

    /**
     * Equality
     */
    bool operator==(const Term& other) const {
        return field_ == other.field_ && *bytes_ == *other.bytes_;
    }

    bool operator!=(const Term& other) const { return !(*this == other); }

    /**
     * Less-than (for std::map, std::set)
     */
    bool operator<(const Term& other) const { return compareTo(other) < 0; }

    /**
     * Equality check (for PhraseQuery)
     */
    bool equals(const Term& other) const { return field_ == other.field_ && *bytes_ == *other.bytes_; }

    /**
     * Hash code (for PhraseQuery)
     */
    size_t hashCode() const {
        size_t h = std::hash<std::string>{}(field_);
        h ^= std::hash<std::string>{}(bytes_->toString()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

    // ==================== String Representation ====================

    std::string toString() const { return field_ + ":" + text(); }

private:
    std::string field_;
    std::shared_ptr<util::BytesRef> bytes_;
};

}  // namespace index
}  // namespace diagon
