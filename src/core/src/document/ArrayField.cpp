// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/ArrayField.h"

namespace diagon {
namespace document {

// ==================== ArrayTextField ====================

FieldType ArrayTextField::TYPE_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    ft.docValuesType = index::DocValuesType::SORTED_SET;
    ft.stored = true;
    ft.tokenized = true;
    ft.omitNorms = false;
    return ft;
}();

FieldType ArrayTextField::TYPE_NOT_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    ft.docValuesType = index::DocValuesType::SORTED_SET;
    ft.stored = false;
    ft.tokenized = true;
    ft.omitNorms = false;
    return ft;
}();

// ==================== ArrayStringField ====================

FieldType ArrayStringField::TYPE_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS;
    ft.docValuesType = index::DocValuesType::SORTED_SET;
    ft.stored = true;
    ft.tokenized = false;
    ft.omitNorms = true;
    return ft;
}();

FieldType ArrayStringField::TYPE_NOT_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS;
    ft.docValuesType = index::DocValuesType::SORTED_SET;
    ft.stored = false;
    ft.tokenized = false;
    ft.omitNorms = true;
    return ft;
}();

// ==================== ArrayNumericField ====================

FieldType ArrayNumericField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.docValuesType = index::DocValuesType::SORTED_NUMERIC;
    ft.stored = false;  // Always stored in doc values
    ft.tokenized = false;
    ft.omitNorms = true;
    return ft;
}();

}  // namespace document
}  // namespace diagon
