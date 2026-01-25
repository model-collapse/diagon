// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/ArrayField.h"

#include <gtest/gtest.h>

using namespace diagon::document;

// ==================== ArrayTextField ====================

class ArrayTextFieldTest : public ::testing::Test {};

TEST_F(ArrayTextFieldTest, ConstructWithValues) {
    std::vector<std::string> values = {"hello world", "search engine", "lucene"};
    ArrayTextField field("tags", values, true);

    EXPECT_EQ(field.name(), "tags");
    EXPECT_EQ(field.getValueCount(), 3);
    EXPECT_EQ(field.getValues(), values);
    EXPECT_TRUE(field.fieldType().stored);
}

TEST_F(ArrayTextFieldTest, ConstructWithMoveValues) {
    std::vector<std::string> values = {"hello", "world"};
    ArrayTextField field("tags", std::move(values), false);

    EXPECT_EQ(field.name(), "tags");
    EXPECT_EQ(field.getValueCount(), 2);
    EXPECT_FALSE(field.fieldType().stored);
}

TEST_F(ArrayTextFieldTest, AddValue) {
    ArrayTextField field("tags", {}, true);

    field.addValue("first");
    field.addValue("second");

    EXPECT_EQ(field.getValueCount(), 2);
    EXPECT_EQ(field.getValues()[0], "first");
    EXPECT_EQ(field.getValues()[1], "second");
}

TEST_F(ArrayTextFieldTest, TokenizeMultipleValues) {
    ArrayTextField field("tags", {"hello world", "search engine"}, false);

    auto tokens = field.tokenize();

    // Should tokenize across all values
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "search");
    EXPECT_EQ(tokens[3], "engine");
}

TEST_F(ArrayTextFieldTest, StringValueReturnsFirst) {
    ArrayTextField field("tags", {"first", "second", "third"}, true);

    auto val = field.stringValue();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "first");
}

TEST_F(ArrayTextFieldTest, EmptyArrayStringValue) {
    ArrayTextField field("tags", {}, true);

    auto val = field.stringValue();
    EXPECT_FALSE(val.has_value());
}

TEST_F(ArrayTextFieldTest, FieldType) {
    ArrayTextField field("tags", {"test"}, true);

    const auto& type = field.fieldType();
    EXPECT_EQ(type.indexOptions, diagon::index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    EXPECT_EQ(type.docValuesType, diagon::index::DocValuesType::SORTED_SET);
    EXPECT_TRUE(type.tokenized);
    EXPECT_FALSE(type.omitNorms);
}

// ==================== ArrayStringField ====================

class ArrayStringFieldTest : public ::testing::Test {};

TEST_F(ArrayStringFieldTest, ConstructWithValues) {
    std::vector<std::string> values = {"electronics", "computers", "laptops"};
    ArrayStringField field("categories", values, true);

    EXPECT_EQ(field.name(), "categories");
    EXPECT_EQ(field.getValueCount(), 3);
    EXPECT_EQ(field.getValues(), values);
}

TEST_F(ArrayStringFieldTest, AddValue) {
    ArrayStringField field("categories", {}, false);

    field.addValue("electronics");
    field.addValue("computers");

    EXPECT_EQ(field.getValueCount(), 2);
    EXPECT_EQ(field.getValues()[0], "electronics");
    EXPECT_EQ(field.getValues()[1], "computers");
}

TEST_F(ArrayStringFieldTest, TokenizeNotTokenized) {
    ArrayStringField field("categories", {"electronics", "computers"}, false);

    auto tokens = field.tokenize();

    // Not tokenized - each value is a single term
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0], "electronics");
    EXPECT_EQ(tokens[1], "computers");
}

TEST_F(ArrayStringFieldTest, GetSortedUniqueValues) {
    ArrayStringField field("categories", {"computers", "electronics", "computers", "laptops"},
                           false);

    auto sorted = field.getSortedUniqueValues();

    // Should be sorted and deduplicated
    ASSERT_EQ(sorted.size(), 3);
    EXPECT_EQ(sorted[0], "computers");
    EXPECT_EQ(sorted[1], "electronics");
    EXPECT_EQ(sorted[2], "laptops");
}

TEST_F(ArrayStringFieldTest, GetSortedUniqueValuesEmpty) {
    ArrayStringField field("categories", {}, false);

    auto sorted = field.getSortedUniqueValues();

    EXPECT_TRUE(sorted.empty());
}

TEST_F(ArrayStringFieldTest, FieldType) {
    ArrayStringField field("categories", {"test"}, false);

    const auto& type = field.fieldType();
    EXPECT_EQ(type.indexOptions, diagon::index::IndexOptions::DOCS);
    EXPECT_EQ(type.docValuesType, diagon::index::DocValuesType::SORTED_SET);
    EXPECT_FALSE(type.tokenized);
    EXPECT_TRUE(type.omitNorms);
}

// ==================== ArrayNumericField ====================

class ArrayNumericFieldTest : public ::testing::Test {};

TEST_F(ArrayNumericFieldTest, ConstructWithValues) {
    std::vector<int64_t> values = {5, 4, 3, 4, 5};
    ArrayNumericField field("ratings", values);

    EXPECT_EQ(field.name(), "ratings");
    EXPECT_EQ(field.getValueCount(), 5);
    EXPECT_EQ(field.getValues(), values);
}

TEST_F(ArrayNumericFieldTest, ConstructWithMoveValues) {
    std::vector<int64_t> values = {1, 2, 3};
    ArrayNumericField field("ratings", std::move(values));

    EXPECT_EQ(field.name(), "ratings");
    EXPECT_EQ(field.getValueCount(), 3);
}

TEST_F(ArrayNumericFieldTest, AddValue) {
    ArrayNumericField field("ratings", {});

    field.addValue(5);
    field.addValue(4);
    field.addValue(3);

    EXPECT_EQ(field.getValueCount(), 3);
    EXPECT_EQ(field.getValues()[0], 5);
    EXPECT_EQ(field.getValues()[1], 4);
    EXPECT_EQ(field.getValues()[2], 3);
}

TEST_F(ArrayNumericFieldTest, GetSortedValues) {
    ArrayNumericField field("ratings", {5, 2, 4, 2, 3, 5});

    auto sorted = field.getSortedValues();

    // Should be sorted but NOT deduplicated (allows duplicates)
    ASSERT_EQ(sorted.size(), 6);
    EXPECT_EQ(sorted[0], 2);
    EXPECT_EQ(sorted[1], 2);
    EXPECT_EQ(sorted[2], 3);
    EXPECT_EQ(sorted[3], 4);
    EXPECT_EQ(sorted[4], 5);
    EXPECT_EQ(sorted[5], 5);
}

TEST_F(ArrayNumericFieldTest, TokenizeReturnsEmpty) {
    ArrayNumericField field("ratings", {1, 2, 3});

    auto tokens = field.tokenize();

    // Numeric fields are not tokenized
    EXPECT_TRUE(tokens.empty());
}

TEST_F(ArrayNumericFieldTest, NumericValueReturnsFirst) {
    ArrayNumericField field("ratings", {5, 4, 3});

    auto val = field.numericValue();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 5);
}

TEST_F(ArrayNumericFieldTest, StringValueReturnsFirstAsString) {
    ArrayNumericField field("ratings", {42, 13});

    auto val = field.stringValue();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "42");
}

TEST_F(ArrayNumericFieldTest, EmptyArrayNumericValue) {
    ArrayNumericField field("ratings", {});

    auto val = field.numericValue();
    EXPECT_FALSE(val.has_value());
}

TEST_F(ArrayNumericFieldTest, FieldType) {
    ArrayNumericField field("ratings", {1, 2});

    const auto& type = field.fieldType();
    EXPECT_EQ(type.indexOptions, diagon::index::IndexOptions::NONE);
    EXPECT_EQ(type.docValuesType, diagon::index::DocValuesType::SORTED_NUMERIC);
    EXPECT_FALSE(type.stored);  // Always stored in doc values
    EXPECT_TRUE(type.omitNorms);
}

// ==================== Integration Test ====================

TEST(ArrayFieldIntegrationTest, AllThreeTypesInDocument) {
    // Simulate creating a document with all three array types
    ArrayTextField tags("tags", {"search", "engine", "database"}, false);
    ArrayStringField categories("categories", {"software", "tools"}, true);
    ArrayNumericField ratings("ratings", {5, 4, 5, 3});

    // Verify types
    EXPECT_TRUE(tags.fieldType().tokenized);
    EXPECT_FALSE(categories.fieldType().tokenized);
    EXPECT_EQ(ratings.fieldType().indexOptions, diagon::index::IndexOptions::NONE);

    // Verify values
    EXPECT_EQ(tags.getValueCount(), 3);
    EXPECT_EQ(categories.getValueCount(), 2);
    EXPECT_EQ(ratings.getValueCount(), 4);
}
