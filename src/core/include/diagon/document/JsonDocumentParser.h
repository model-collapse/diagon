// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace document {

/**
 * JsonDocumentParser - Zero-copy JSON to Document conversion
 *
 * Parses JSON strings into Diagon Documents, eliminating per-field CGO
 * overhead for language bindings (Go, Python, etc.). Instead of 10-15
 * CGO calls per document for field-by-field construction, a single
 * JSON string can be passed and parsed entirely in C++.
 *
 * Field type mapping:
 *   JSON string  -> TextField (tokenized, stored)
 *   JSON integer -> Field with indexed long FieldType
 *   JSON float   -> Field with indexed double FieldType
 *   JSON boolean -> StringField ("true"/"false", stored)
 *   JSON null    -> skipped
 *   JSON object  -> flattened with dot notation ("a.b.c")
 *   JSON array   -> multiple fields with same name (Lucene multi-value)
 */
class JsonDocumentParser {
public:
    /**
     * Parse a single JSON object into a Document.
     *
     * @param json JSON string (must be a JSON object)
     * @param len Length of JSON string
     * @return Parsed Document, or nullptr on parse error (check diagon_last_error())
     */
    static std::unique_ptr<Document> parse(const char* json, size_t len);

    /**
     * Parse a single JSON object with an explicit document ID.
     *
     * The id is added as a StringField named "_id".
     *
     * @param json JSON string (must be a JSON object)
     * @param len Length of JSON string
     * @param id Document ID string
     * @return Parsed Document, or nullptr on parse error
     */
    static std::unique_ptr<Document> parseWithId(const char* json, size_t len, const char* id);

    /**
     * Parse a JSON array of objects into a vector of Documents.
     *
     * @param json JSON string (must be a JSON array of objects)
     * @param len Length of JSON string
     * @return Vector of parsed Documents (empty on parse error)
     */
    static std::vector<std::unique_ptr<Document>> parseBatch(const char* json, size_t len);
};

}  // namespace document
}  // namespace diagon
