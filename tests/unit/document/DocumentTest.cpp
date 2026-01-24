// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"

#include "diagon/document/Field.h"

#include <gtest/gtest.h>

using namespace diagon::document;
using namespace diagon::index;  // For IndexOptions, DocValuesType

// ==================== Field Tests ====================

TEST(FieldTest, TextFieldCreation) {
    TextField field("title", "hello world");

    EXPECT_EQ(field.name(), "title");
    EXPECT_EQ(field.stringValue(), "hello world");
    EXPECT_TRUE(field.fieldType().tokenized);
    EXPECT_EQ(field.fieldType().indexOptions, IndexOptions::DOCS_AND_FREQS);
}

TEST(FieldTest, StringFieldCreation) {
    StringField field("id", "doc123");

    EXPECT_EQ(field.name(), "id");
    EXPECT_EQ(field.stringValue(), "doc123");
    EXPECT_FALSE(field.fieldType().tokenized);
    EXPECT_EQ(field.fieldType().indexOptions, IndexOptions::DOCS_AND_FREQS);
}

TEST(FieldTest, NumericDocValuesFieldCreation) {
    NumericDocValuesField field("score", 42);

    EXPECT_EQ(field.name(), "score");
    EXPECT_EQ(field.numericValue(), 42);
    EXPECT_EQ(field.fieldType().docValuesType, DocValuesType::NUMERIC);
    EXPECT_EQ(field.fieldType().indexOptions, IndexOptions::NONE);
}

TEST(FieldTest, TokenizationWhitespace) {
    TextField field("text", "hello world test");

    auto tokens = field.tokenize();
    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "test");
}

TEST(FieldTest, TokenizationMultipleSpaces) {
    TextField field("text", "hello   world\t\ntest");

    auto tokens = field.tokenize();
    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "test");
}

TEST(FieldTest, TokenizationEmptyString) {
    TextField field("text", "");

    auto tokens = field.tokenize();
    EXPECT_EQ(tokens.size(), 0);
}

TEST(FieldTest, StringFieldNotTokenized) {
    StringField field("id", "word1 word2 word3");

    auto tokens = field.tokenize();
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0], "word1 word2 word3");
}

// ==================== Document Tests ====================

TEST(DocumentTest, EmptyDocument) {
    Document doc;

    EXPECT_TRUE(doc.empty());
    EXPECT_EQ(doc.size(), 0);
}

TEST(DocumentTest, AddSingleField) {
    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello"));

    EXPECT_FALSE(doc.empty());
    EXPECT_EQ(doc.size(), 1);
}

TEST(DocumentTest, AddMultipleFields) {
    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello world"));
    doc.add(std::make_unique<StringField>("id", "doc1"));
    doc.add(std::make_unique<NumericDocValuesField>("score", 100));

    EXPECT_EQ(doc.size(), 3);
}

TEST(DocumentTest, GetFieldByName) {
    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello world"));
    doc.add(std::make_unique<StringField>("id", "doc1"));

    auto* titleField = doc.getField("title");
    ASSERT_NE(titleField, nullptr);
    EXPECT_EQ(titleField->name(), "title");
    EXPECT_EQ(titleField->stringValue(), "hello world");

    auto* idField = doc.getField("id");
    ASSERT_NE(idField, nullptr);
    EXPECT_EQ(idField->name(), "id");
    EXPECT_EQ(idField->stringValue(), "doc1");

    auto* missingField = doc.getField("missing");
    EXPECT_EQ(missingField, nullptr);
}

TEST(DocumentTest, GetStringValue) {
    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello world"));

    auto value = doc.get("title");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "hello world");

    auto missing = doc.get("missing");
    EXPECT_FALSE(missing.has_value());
}

TEST(DocumentTest, MultipleFieldsSameName) {
    Document doc;
    doc.add(std::make_unique<TextField>("category", "sports"));
    doc.add(std::make_unique<TextField>("category", "news"));
    doc.add(std::make_unique<TextField>("category", "politics"));

    EXPECT_EQ(doc.size(), 3);

    auto fields = doc.getFieldsByName("category");
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0]->stringValue(), "sports");
    EXPECT_EQ(fields[1]->stringValue(), "news");
    EXPECT_EQ(fields[2]->stringValue(), "politics");

    // getField returns first one
    auto* firstField = doc.getField("category");
    ASSERT_NE(firstField, nullptr);
    EXPECT_EQ(firstField->stringValue(), "sports");
}

TEST(DocumentTest, ClearDocument) {
    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello"));
    doc.add(std::make_unique<StringField>("id", "doc1"));

    EXPECT_EQ(doc.size(), 2);

    doc.clear();

    EXPECT_TRUE(doc.empty());
    EXPECT_EQ(doc.size(), 0);
}
