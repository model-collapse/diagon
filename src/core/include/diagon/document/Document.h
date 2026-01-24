// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/IndexableField.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace document {

/**
 * Document - Collection of fields to be indexed
 *
 * Based on: org.apache.lucene.document.Document
 */
class Document {
private:
    std::vector<std::unique_ptr<IndexableField>> fields_;

public:
    Document() = default;

    // Disable copy (fields contain unique_ptr)
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Enable move
    Document(Document&&) = default;
    Document& operator=(Document&&) = default;

    /**
     * Add a field to the document
     */
    void add(std::unique_ptr<IndexableField> field) {
        fields_.push_back(std::move(field));
    }

    /**
     * Get all fields
     */
    const std::vector<std::unique_ptr<IndexableField>>& getFields() const {
        return fields_;
    }

    /**
     * Get first field with given name
     * @return pointer to field, or nullptr if not found
     */
    const IndexableField* getField(const std::string& name) const {
        for (const auto& field : fields_) {
            if (field->name() == name) {
                return field.get();
            }
        }
        return nullptr;
    }

    /**
     * Get all fields with given name
     */
    std::vector<const IndexableField*> getFieldsByName(const std::string& name) const {
        std::vector<const IndexableField*> result;
        for (const auto& field : fields_) {
            if (field->name() == name) {
                result.push_back(field.get());
            }
        }
        return result;
    }

    /**
     * Get string value of first field with given name
     */
    std::optional<std::string> get(const std::string& name) const {
        auto* field = getField(name);
        if (!field) {
            return std::nullopt;
        }
        return field->stringValue();
    }

    /**
     * Get number of fields
     */
    size_t size() const {
        return fields_.size();
    }

    /**
     * Check if document is empty
     */
    bool empty() const {
        return fields_.empty();
    }

    /**
     * Clear all fields
     */
    void clear() {
        fields_.clear();
    }
};

}  // namespace document
}  // namespace diagon
