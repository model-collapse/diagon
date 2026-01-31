// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>

namespace diagon {
namespace index {

// Forward declarations
class Terms;

/**
 * Fields - Iterator over fields and their terms
 *
 * Based on: org.apache.lucene.index.Fields
 *
 * Provides access to the inverted index structure:
 * - Iterate over all indexed fields
 * - Get Terms for a specific field
 *
 * This is the "pull" API used by FieldsConsumer during indexing:
 * - Producer creates Fields implementation (wraps in-memory postings)
 * - Consumer iterates and writes to disk format
 *
 * Thread Safety: Implementations are NOT thread-safe
 */
class Fields {
public:
    virtual ~Fields() = default;

    /**
     * Get Terms for a specific field
     *
     * @param field Field name
     * @return Terms for field, or nullptr if field doesn't exist
     */
    virtual std::unique_ptr<Terms> terms(const std::string& field) = 0;

    /**
     * Get number of fields
     *
     * @return Number of fields, or -1 if unknown
     */
    virtual int size() const = 0;

    /**
     * Iterator over field names
     *
     * @return Iterator over field names (in sorted order)
     */
    class Iterator {
    public:
        virtual ~Iterator() = default;

        /**
         * Check if more fields available
         */
        virtual bool hasNext() const = 0;

        /**
         * Get next field name
         */
        virtual std::string next() = 0;
    };

    /**
     * Create iterator over field names
     *
     * @return Iterator over field names
     */
    virtual std::unique_ptr<Iterator> iterator() = 0;
};

}  // namespace index
}  // namespace diagon
