// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/JsonDocumentParser.h"

#include "diagon/document/Field.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace diagon {
namespace document {

namespace {

// Reusable indexed-long FieldType (matches C API diagon_create_indexed_long_field)
FieldType makeIndexedLongType() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS;
    ft.stored = true;
    ft.tokenized = false;
    ft.docValuesType = index::DocValuesType::NUMERIC;
    ft.numericType = NumericType::LONG;
    return ft;
}

// Reusable indexed-double FieldType (matches C API diagon_create_indexed_double_field)
FieldType makeIndexedDoubleType() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS;
    ft.stored = true;
    ft.tokenized = false;
    ft.docValuesType = index::DocValuesType::NUMERIC;
    ft.numericType = NumericType::DOUBLE;
    return ft;
}

static const FieldType INDEXED_LONG_TYPE = makeIndexedLongType();
static const FieldType INDEXED_DOUBLE_TYPE = makeIndexedDoubleType();

// Maximum nesting depth to prevent stack overflow from malicious input.
// 64 levels is generous for real-world JSON while keeping stack usage bounded.
static constexpr int kMaxNestingDepth = 64;

// Forward declaration
void addValue(Document& doc, const std::string& fieldName, const nlohmann::json& val, int depth);

/**
 * Add fields from a JSON object to a document, handling nested objects via
 * dot-path flattening and arrays via multi-value fields.
 */
void addFields(Document& doc, const nlohmann::json& j, const std::string& prefix, int depth = 0) {
    if (depth > kMaxNestingDepth) {
        throw std::runtime_error("JSON nesting depth exceeds limit (" +
                                 std::to_string(kMaxNestingDepth) + ")");
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        std::string fieldName = prefix.empty() ? it.key() : prefix + "." + it.key();
        addValue(doc, fieldName, it.value(), depth);
    }
}

/**
 * Add a single JSON value as a document field.
 * Handles all JSON types uniformly — used by both top-level objects and array elements.
 */
void addValue(Document& doc, const std::string& fieldName, const nlohmann::json& val, int depth) {
    if (val.is_null()) {
        return;
    }

    if (val.is_string()) {
        doc.add(std::make_unique<TextField>(fieldName, val.get<std::string>(), true));
    } else if (val.is_boolean()) {
        doc.add(
            std::make_unique<StringField>(fieldName, val.get<bool>() ? "true" : "false", true));
    } else if (val.is_number_unsigned()) {
        // Must check before is_number_integer(): unsigned values > INT64_MAX
        // are stored as uint64_t by nlohmann and would be silently dropped otherwise.
        // We reinterpret as int64_t (bit pattern preserved, Lucene convention).
        uint64_t uval = val.get<uint64_t>();
        int64_t longVal = static_cast<int64_t>(uval);
        doc.add(std::make_unique<Field>(fieldName, longVal, INDEXED_LONG_TYPE));
    } else if (val.is_number_integer()) {
        int64_t longVal = val.get<int64_t>();
        doc.add(std::make_unique<Field>(fieldName, longVal, INDEXED_LONG_TYPE));
    } else if (val.is_number_float()) {
        double dblVal = val.get<double>();
        int64_t longBits = std::bit_cast<int64_t>(dblVal);
        doc.add(std::make_unique<Field>(fieldName, longBits, INDEXED_DOUBLE_TYPE));
    } else if (val.is_object()) {
        addFields(doc, val, fieldName, depth + 1);
    } else if (val.is_array()) {
        // Array: add one field per element (Lucene multi-value pattern)
        // Nested arrays are flattened into the same field name.
        for (const auto& elem : val) {
            addValue(doc, fieldName, elem, depth);
        }
    }
}

}  // anonymous namespace

std::unique_ptr<Document> JsonDocumentParser::parse(const char* json, size_t len) {
    nlohmann::json j = nlohmann::json::parse(json, json + len, nullptr, false);
    if (j.is_discarded()) {
        throw std::runtime_error("Invalid JSON: parse failed");
    }
    if (!j.is_object()) {
        throw std::runtime_error("JSON must be an object, got " + std::string(j.type_name()));
    }

    auto doc = std::make_unique<Document>();
    addFields(*doc, j, "");
    return doc;
}

std::unique_ptr<Document> JsonDocumentParser::parseWithId(const char* json, size_t len,
                                                          const char* id) {
    auto doc = parse(json, len);
    if (doc && id) {
        doc->add(std::make_unique<StringField>("_id", std::string(id), true));
    }
    return doc;
}

std::vector<std::unique_ptr<Document>> JsonDocumentParser::parseBatch(const char* json,
                                                                      size_t len) {
    nlohmann::json j = nlohmann::json::parse(json, json + len, nullptr, false);
    if (j.is_discarded()) {
        throw std::runtime_error("Invalid JSON: parse failed");
    }
    if (!j.is_array()) {
        throw std::runtime_error("JSON must be an array for batch parsing, got " +
                                 std::string(j.type_name()));
    }

    std::vector<std::unique_ptr<Document>> docs;
    docs.reserve(j.size());

    for (size_t i = 0; i < j.size(); ++i) {
        const auto& elem = j[i];
        if (!elem.is_object()) {
            throw std::runtime_error("Array element " + std::to_string(i) +
                                     " must be an object, got " + std::string(elem.type_name()));
        }
        auto doc = std::make_unique<Document>();
        addFields(*doc, elem, "");
        docs.push_back(std::move(doc));
    }

    return docs;
}

}  // namespace document
}  // namespace diagon
