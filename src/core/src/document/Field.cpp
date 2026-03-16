// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Field.h"

namespace diagon {
namespace document {

// TextField field types
FieldType TextField::TYPE_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS;
    ft.stored = true;
    ft.tokenized = true;
    return ft;
}();

FieldType TextField::TYPE_NOT_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS;
    ft.stored = false;
    ft.tokenized = true;
    return ft;
}();

// StringField field types
FieldType StringField::TYPE_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS;
    ft.stored = true;
    ft.tokenized = false;
    return ft;
}();

FieldType StringField::TYPE_NOT_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS;
    ft.stored = false;
    ft.tokenized = false;
    return ft;
}();

// NumericDocValuesField field type
FieldType NumericDocValuesField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;  // Not indexed in inverted index
    ft.docValuesType = index::DocValuesType::NUMERIC;
    ft.stored = false;
    ft.tokenized = false;
    return ft;
}();

// LongPointField field type
FieldType LongPointField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;  // Not indexed in inverted index
    ft.stored = false;
    ft.tokenized = false;
    ft.pointDimensionCount = 1;
    ft.pointIndexDimensionCount = 1;
    ft.pointNumBytes = 8;  // sizeof(int64_t)
    return ft;
}();

// DoublePointField field type
FieldType DoublePointField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.stored = false;
    ft.tokenized = false;
    ft.pointDimensionCount = 1;
    ft.pointIndexDimensionCount = 1;
    ft.pointNumBytes = 8;  // sizeof(double) as sortable long
    return ft;
}();

// SortedDocValuesField field type
FieldType SortedDocValuesField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.docValuesType = index::DocValuesType::SORTED;
    ft.stored = false;
    ft.tokenized = false;
    return ft;
}();

// BinaryDocValuesField field type
FieldType BinaryDocValuesField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.docValuesType = index::DocValuesType::BINARY;
    ft.stored = false;
    ft.tokenized = false;
    return ft;
}();

// SortedNumericDocValuesField field type
FieldType SortedNumericDocValuesField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.docValuesType = index::DocValuesType::SORTED_NUMERIC;
    ft.stored = false;
    ft.tokenized = false;
    return ft;
}();

// SortedSetDocValuesField field type
FieldType SortedSetDocValuesField::TYPE = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.docValuesType = index::DocValuesType::SORTED_SET;
    ft.stored = false;
    ft.tokenized = false;
    return ft;
}();

}  // namespace document
}  // namespace diagon
