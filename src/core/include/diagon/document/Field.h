// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/IndexableField.h"

#include <memory>
#include <sstream>
#include <string>

namespace diagon {
namespace document {

/**
 * Field - Basic implementation of IndexableField
 *
 * Based on: org.apache.lucene.document.Field
 */
class Field : public IndexableField {
protected:
    std::string name_;
    FieldType type_;
    std::string stringValue_;
    std::optional<int64_t> numericValue_;

    // Protected constructor for subclasses
    Field(std::string name, FieldType type)
        : name_(std::move(name))
        , type_(type) {}

public:
    /**
     * Create a field with string value
     */
    Field(std::string name, std::string value, FieldType type)
        : name_(std::move(name))
        , type_(type)
        , stringValue_(std::move(value)) {}

    /**
     * Create a field with numeric value
     */
    Field(std::string name, int64_t value, FieldType type)
        : name_(std::move(name))
        , type_(type)
        , numericValue_(value) {}

    // IndexableField implementation
    std::string name() const override { return name_; }

    const FieldType& fieldType() const override { return type_; }

    std::optional<std::string> stringValue() const override {
        if (!stringValue_.empty()) {
            return stringValue_;
        }
        if (numericValue_) {
            return std::to_string(*numericValue_);
        }
        return std::nullopt;
    }

    std::optional<int64_t> numericValue() const override { return numericValue_; }

    std::optional<util::BytesRef> binaryValue() const override {
        // Phase 2: not supporting binary fields yet
        return std::nullopt;
    }

    /**
     * Simple whitespace tokenization
     * Splits on space, tab, newline
     */
    std::vector<std::string> tokenize() const override {
        std::vector<std::string> tokens;

        if (!type_.tokenized) {
            // Not tokenized - return whole value as single token
            auto val = stringValue();
            if (val) {
                tokens.push_back(*val);
            }
            return tokens;
        }

        // Tokenized - split on whitespace
        auto val = stringValue();
        if (!val) {
            return tokens;
        }

        std::istringstream iss(*val);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }

        return tokens;
    }
};

/**
 * TextField - Tokenized text field
 *
 * Based on: org.apache.lucene.document.TextField
 */
class TextField : public Field {
public:
    // Predefined field types
    static FieldType TYPE_STORED;
    static FieldType TYPE_NOT_STORED;

    TextField(std::string name, std::string value, bool stored = false)
        : Field(std::move(name), std::move(value), stored ? TYPE_STORED : TYPE_NOT_STORED) {}

    explicit TextField(std::string name, std::string value, FieldType type)
        : Field(std::move(name), std::move(value), type) {}
};

/**
 * StringField - Non-tokenized keyword field
 *
 * Based on: org.apache.lucene.document.StringField
 */
class StringField : public Field {
public:
    // Predefined field types
    static FieldType TYPE_STORED;
    static FieldType TYPE_NOT_STORED;

    StringField(std::string name, std::string value, bool stored = false)
        : Field(std::move(name), std::move(value), stored ? TYPE_STORED : TYPE_NOT_STORED) {}

    explicit StringField(std::string name, std::string value, FieldType type)
        : Field(std::move(name), std::move(value), type) {}
};

/**
 * NumericDocValuesField - Numeric column value
 *
 * Based on: org.apache.lucene.document.NumericDocValuesField
 */
class NumericDocValuesField : public Field {
public:
    static FieldType TYPE;

    NumericDocValuesField(std::string name, int64_t value)
        : Field(std::move(name), value, TYPE) {}

    std::vector<std::string> tokenize() const override {
        // Doc values fields are not tokenized for inverted index
        return {};
    }
};

}  // namespace document
}  // namespace diagon
