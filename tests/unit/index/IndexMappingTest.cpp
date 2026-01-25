// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexMapping.h"

#include <gtest/gtest.h>

using namespace diagon::index;

class IndexMappingTest : public ::testing::Test {
protected:
    IndexMapping mapping;
};

// ==================== Single-Valued Fields ====================

TEST_F(IndexMappingTest, AddSingleValuedField) {
    mapping.addField("title", IndexOptions::DOCS_AND_FREQS_AND_POSITIONS, DocValuesType::NONE,
                     true, true, false);

    EXPECT_TRUE(mapping.hasField("title"));
    EXPECT_FALSE(mapping.isMultiValued("title"));

    auto* info = mapping.getFieldInfo("title");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "title");
    EXPECT_EQ(info->indexOptions, IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    EXPECT_FALSE(info->multiValued);
}

TEST_F(IndexMappingTest, AddNumericField) {
    mapping.addField("price", IndexOptions::NONE, DocValuesType::NUMERIC, false, false, true);

    EXPECT_TRUE(mapping.hasField("price"));
    EXPECT_FALSE(mapping.isMultiValued("price"));

    auto* info = mapping.getFieldInfo("price");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->docValuesType, DocValuesType::NUMERIC);
    EXPECT_FALSE(info->multiValued);
}

TEST_F(IndexMappingTest, AddDuplicateFieldThrows) {
    mapping.addField("title", IndexOptions::DOCS, DocValuesType::NONE, true, false, false);

    EXPECT_THROW(
        mapping.addField("title", IndexOptions::DOCS_AND_FREQS, DocValuesType::NONE, false, true,
                         false),
        std::invalid_argument);
}

// ==================== Array Fields ====================

TEST_F(IndexMappingTest, AddArrayTextField) {
    mapping.addArrayField("tags", ArrayElementType::TEXT, true);

    EXPECT_TRUE(mapping.hasField("tags"));
    EXPECT_TRUE(mapping.isMultiValued("tags"));

    auto elementType = mapping.getElementType("tags");
    ASSERT_TRUE(elementType.has_value());
    EXPECT_EQ(*elementType, ArrayElementType::TEXT);

    auto* info = mapping.getFieldInfo("tags");
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->multiValued);
    EXPECT_EQ(info->indexOptions, IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    EXPECT_EQ(info->docValuesType, DocValuesType::SORTED_SET);
    EXPECT_FALSE(info->omitNorms);
}

TEST_F(IndexMappingTest, AddArrayStringField) {
    mapping.addArrayField("categories", ArrayElementType::STRING, false);

    EXPECT_TRUE(mapping.hasField("categories"));
    EXPECT_TRUE(mapping.isMultiValued("categories"));

    auto elementType = mapping.getElementType("categories");
    ASSERT_TRUE(elementType.has_value());
    EXPECT_EQ(*elementType, ArrayElementType::STRING);

    auto* info = mapping.getFieldInfo("categories");
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->multiValued);
    EXPECT_EQ(info->indexOptions, IndexOptions::DOCS);
    EXPECT_EQ(info->docValuesType, DocValuesType::SORTED_SET);
    EXPECT_TRUE(info->omitNorms);
}

TEST_F(IndexMappingTest, AddArrayNumericField) {
    mapping.addArrayField("ratings", ArrayElementType::NUMERIC, false);

    EXPECT_TRUE(mapping.hasField("ratings"));
    EXPECT_TRUE(mapping.isMultiValued("ratings"));

    auto elementType = mapping.getElementType("ratings");
    ASSERT_TRUE(elementType.has_value());
    EXPECT_EQ(*elementType, ArrayElementType::NUMERIC);

    auto* info = mapping.getFieldInfo("ratings");
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->multiValued);
    EXPECT_EQ(info->indexOptions, IndexOptions::NONE);
    EXPECT_EQ(info->docValuesType, DocValuesType::SORTED_NUMERIC);
    EXPECT_TRUE(info->omitNorms);
}

TEST_F(IndexMappingTest, AddDuplicateArrayFieldThrows) {
    mapping.addArrayField("tags", ArrayElementType::TEXT, true);

    EXPECT_THROW(mapping.addArrayField("tags", ArrayElementType::STRING, false),
                 std::invalid_argument);
}

// ==================== Mixed Fields ====================

TEST_F(IndexMappingTest, MixedSingleAndArrayFields) {
    mapping.addField("title", IndexOptions::DOCS_AND_FREQS_AND_POSITIONS, DocValuesType::NONE,
                     true, true, false);
    mapping.addArrayField("tags", ArrayElementType::TEXT, false);
    mapping.addField("price", IndexOptions::NONE, DocValuesType::NUMERIC, false, false, true);
    mapping.addArrayField("categories", ArrayElementType::STRING, true);

    EXPECT_EQ(mapping.size(), 4);

    EXPECT_FALSE(mapping.isMultiValued("title"));
    EXPECT_TRUE(mapping.isMultiValued("tags"));
    EXPECT_FALSE(mapping.isMultiValued("price"));
    EXPECT_TRUE(mapping.isMultiValued("categories"));
}

// ==================== Queries ====================

TEST_F(IndexMappingTest, NonExistentField) {
    EXPECT_FALSE(mapping.hasField("nonexistent"));
    EXPECT_FALSE(mapping.isMultiValued("nonexistent"));
    EXPECT_EQ(mapping.getFieldInfo("nonexistent"), nullptr);
    EXPECT_FALSE(mapping.getElementType("nonexistent").has_value());
}

TEST_F(IndexMappingTest, GetElementTypeForSingleValuedField) {
    mapping.addField("title", IndexOptions::DOCS, DocValuesType::NONE, true, false, false);

    auto elementType = mapping.getElementType("title");
    EXPECT_FALSE(elementType.has_value());
}

TEST_F(IndexMappingTest, FieldNames) {
    mapping.addField("title", IndexOptions::DOCS, DocValuesType::NONE, true, false, false);
    mapping.addArrayField("tags", ArrayElementType::TEXT, false);
    mapping.addField("price", IndexOptions::NONE, DocValuesType::NUMERIC, false, false, true);

    auto names = mapping.fieldNames();
    EXPECT_EQ(names.size(), 3);

    // Names should include all three fields
    EXPECT_NE(std::find(names.begin(), names.end(), "title"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "tags"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "price"), names.end());
}

TEST_F(IndexMappingTest, EmptyMapping) {
    EXPECT_EQ(mapping.size(), 0);
    EXPECT_TRUE(mapping.fieldNames().empty());
}
