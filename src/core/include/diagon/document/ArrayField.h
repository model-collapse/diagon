// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/IndexableField.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace diagon {
namespace document {

/**
 * ArrayTextField - Multi-valued text field (tokenized, full-text searchable)
 *
 * Each value is tokenized separately and all terms are indexed.
 * Positions are preserved for phrase queries across values.
 *
 * Based on: Lucene SORTED_SET + ClickHouse Array(String)
 */
class ArrayTextField : public IndexableField {
public:
    static FieldType TYPE_STORED;
    static FieldType TYPE_NOT_STORED;

    ArrayTextField(std::string name, const std::vector<std::string>& values, bool stored = false)
        : name_(std::move(name))
        , values_(values)
        , type_(stored ? TYPE_STORED : TYPE_NOT_STORED) {}

    ArrayTextField(std::string name, std::vector<std::string>&& values, bool stored = false)
        : name_(std::move(name))
        , values_(std::move(values))
        , type_(stored ? TYPE_STORED : TYPE_NOT_STORED) {}

    // Add value to array
    void addValue(const std::string& value) { values_.push_back(value); }

    void addValue(std::string&& value) { values_.push_back(std::move(value)); }

    // Access
    const std::vector<std::string>& getValues() const { return values_; }
    size_t getValueCount() const { return values_.size(); }

    // IndexableField interface
    std::string name() const override { return name_; }

    const FieldType& fieldType() const override { return type_; }

    std::optional<std::string> stringValue() const override {
        // Return first value for single-value access
        if (values_.empty()) {
            return std::nullopt;
        }
        return values_[0];
    }

    std::optional<int64_t> numericValue() const override { return std::nullopt; }

    std::optional<util::BytesRef> binaryValue() const override { return std::nullopt; }

    /**
     * Tokenize all array values
     * Positions are continuous across values
     */
    std::vector<std::string> tokenize() const override {
        std::vector<std::string> allTokens;

        for (const auto& value : values_) {
            // Simple whitespace tokenization
            std::istringstream iss(value);
            std::string token;
            while (iss >> token) {
                allTokens.push_back(token);
            }
        }

        return allTokens;
    }

private:
    std::string name_;
    std::vector<std::string> values_;
    FieldType type_;
};

/**
 * ArrayStringField - Multi-valued string field (exact match, not tokenized)
 *
 * Each value treated as single term for exact matching.
 * Values are deduplicated and sorted within document.
 *
 * Based on: Lucene SORTED_SET + ClickHouse Array(String)
 */
class ArrayStringField : public IndexableField {
public:
    static FieldType TYPE_STORED;
    static FieldType TYPE_NOT_STORED;

    ArrayStringField(std::string name, const std::vector<std::string>& values, bool stored = false)
        : name_(std::move(name))
        , values_(values)
        , type_(stored ? TYPE_STORED : TYPE_NOT_STORED) {}

    ArrayStringField(std::string name, std::vector<std::string>&& values, bool stored = false)
        : name_(std::move(name))
        , values_(std::move(values))
        , type_(stored ? TYPE_STORED : TYPE_NOT_STORED) {}

    void addValue(const std::string& value) { values_.push_back(value); }

    void addValue(std::string&& value) { values_.push_back(std::move(value)); }

    const std::vector<std::string>& getValues() const { return values_; }
    size_t getValueCount() const { return values_.size(); }

    // IndexableField interface
    std::string name() const override { return name_; }

    const FieldType& fieldType() const override { return type_; }

    std::optional<std::string> stringValue() const override {
        if (values_.empty()) {
            return std::nullopt;
        }
        return values_[0];
    }

    std::optional<int64_t> numericValue() const override { return std::nullopt; }

    std::optional<util::BytesRef> binaryValue() const override { return std::nullopt; }

    /**
     * Not tokenized - return each value as-is
     */
    std::vector<std::string> tokenize() const override {
        // Not tokenized - each value is a single term
        return values_;
    }

    /**
     * Get sorted and deduplicated values
     * Used during indexing for SORTED_SET storage
     */
    std::vector<std::string> getSortedUniqueValues() const {
        std::vector<std::string> sorted = values_;
        std::sort(sorted.begin(), sorted.end());
        auto last = std::unique(sorted.begin(), sorted.end());
        sorted.erase(last, sorted.end());
        return sorted;
    }

private:
    std::string name_;
    std::vector<std::string> values_;
    FieldType type_;
};

/**
 * ArrayNumericField - Multi-valued numeric field (range queries, sorting)
 *
 * Stored in column format for efficient filtering.
 * Values are sorted within document.
 *
 * Based on: Lucene SORTED_NUMERIC + ClickHouse Array(Int64)
 */
class ArrayNumericField : public IndexableField {
public:
    static FieldType TYPE;

    ArrayNumericField(std::string name, const std::vector<int64_t>& values)
        : name_(std::move(name))
        , values_(values) {}

    ArrayNumericField(std::string name, std::vector<int64_t>&& values)
        : name_(std::move(name))
        , values_(std::move(values)) {}

    void addValue(int64_t value) { values_.push_back(value); }

    const std::vector<int64_t>& getValues() const { return values_; }
    size_t getValueCount() const { return values_.size(); }

    // IndexableField interface
    std::string name() const override { return name_; }

    const FieldType& fieldType() const override { return TYPE; }

    std::optional<std::string> stringValue() const override {
        // Return first value as string
        if (values_.empty()) {
            return std::nullopt;
        }
        return std::to_string(values_[0]);
    }

    std::optional<int64_t> numericValue() const override {
        // Return first value
        if (values_.empty()) {
            return std::nullopt;
        }
        return values_[0];
    }

    std::optional<util::BytesRef> binaryValue() const override { return std::nullopt; }

    /**
     * Doc values fields are not tokenized for inverted index
     */
    std::vector<std::string> tokenize() const override { return {}; }

    /**
     * Get sorted values (NOT deduplicated - allows duplicates)
     * Used during indexing for SORTED_NUMERIC storage
     */
    std::vector<int64_t> getSortedValues() const {
        std::vector<int64_t> sorted = values_;
        std::sort(sorted.begin(), sorted.end());
        return sorted;
    }

private:
    std::string name_;
    std::vector<int64_t> values_;
};

}  // namespace document
}  // namespace diagon
