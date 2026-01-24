// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FieldInfo.h"

#include <gtest/gtest.h>

using namespace diagon::index;

class FieldInfoTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ==================== Enum Tests ====================

TEST_F(FieldInfoTest, IndexOptionsValues) {
    EXPECT_EQ(0, static_cast<uint8_t>(IndexOptions::NONE));
    EXPECT_EQ(1, static_cast<uint8_t>(IndexOptions::DOCS));
    EXPECT_EQ(2, static_cast<uint8_t>(IndexOptions::DOCS_AND_FREQS));
    EXPECT_EQ(3, static_cast<uint8_t>(IndexOptions::DOCS_AND_FREQS_AND_POSITIONS));
    EXPECT_EQ(4, static_cast<uint8_t>(IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS));
}

TEST_F(FieldInfoTest, DocValuesTypeValues) {
    EXPECT_EQ(0, static_cast<uint8_t>(DocValuesType::NONE));
    EXPECT_EQ(1, static_cast<uint8_t>(DocValuesType::NUMERIC));
    EXPECT_EQ(2, static_cast<uint8_t>(DocValuesType::BINARY));
    EXPECT_EQ(3, static_cast<uint8_t>(DocValuesType::SORTED));
    EXPECT_EQ(4, static_cast<uint8_t>(DocValuesType::SORTED_NUMERIC));
    EXPECT_EQ(5, static_cast<uint8_t>(DocValuesType::SORTED_SET));
}

TEST_F(FieldInfoTest, DocValuesSkipIndexTypeValues) {
    EXPECT_EQ(0, static_cast<uint8_t>(DocValuesSkipIndexType::NONE));
    EXPECT_EQ(1, static_cast<uint8_t>(DocValuesSkipIndexType::RANGE));
}

// ==================== FieldInfo Basic Tests ====================

TEST_F(FieldInfoTest, DefaultConstruction) {
    FieldInfo info;
    EXPECT_TRUE(info.name.empty());
    EXPECT_EQ(-1, info.number);
    EXPECT_EQ(IndexOptions::NONE, info.indexOptions);
    EXPECT_FALSE(info.storeTermVector);
    EXPECT_FALSE(info.omitNorms);
    EXPECT_FALSE(info.storePayloads);
    EXPECT_EQ(DocValuesType::NONE, info.docValuesType);
    EXPECT_EQ(DocValuesSkipIndexType::NONE, info.docValuesSkipIndex);
    EXPECT_EQ(-1, info.dvGen);
    EXPECT_EQ(0, info.pointDimensionCount);
    EXPECT_EQ(0, info.pointIndexDimensionCount);
    EXPECT_EQ(0, info.pointNumBytes);
    EXPECT_FALSE(info.softDeletesField);
    EXPECT_FALSE(info.isParentField);
    EXPECT_TRUE(info.attributes.empty());
}

TEST_F(FieldInfoTest, ValidFieldInfo) {
    FieldInfo info;
    info.name = "title";
    info.number = 0;
    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;

    EXPECT_NO_THROW(info.validate());
}

TEST_F(FieldInfoTest, ValidationEmptyName) {
    FieldInfo info;
    info.name = "";
    info.number = 0;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationNegativeNumber) {
    FieldInfo info;
    info.name = "field";
    info.number = -1;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationTermVectorWithoutIndex) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.indexOptions = IndexOptions::NONE;
    info.storeTermVector = true;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationPayloadsWithoutIndex) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.indexOptions = IndexOptions::NONE;
    info.storePayloads = true;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationPayloadsWithoutPositions) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.indexOptions = IndexOptions::DOCS_AND_FREQS;
    info.storePayloads = true;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationSkipIndexIncompatibleDocValues) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.docValuesType = DocValuesType::BINARY;
    info.docValuesSkipIndex = DocValuesSkipIndexType::RANGE;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationPointValuesInconsistent) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.pointDimensionCount = 2;
    info.pointIndexDimensionCount = 0;  // Invalid
    info.pointNumBytes = 4;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationPointNumBytesZero) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.pointDimensionCount = 2;
    info.pointIndexDimensionCount = 2;
    info.pointNumBytes = 0;  // Invalid

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

TEST_F(FieldInfoTest, ValidationBothSoftDeletesAndParent) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;
    info.softDeletesField = true;
    info.isParentField = true;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}

// ==================== FieldInfo Utility Methods ====================

TEST_F(FieldInfoTest, HasPostings) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.indexOptions = IndexOptions::NONE;
    EXPECT_FALSE(info.hasPostings());

    info.indexOptions = IndexOptions::DOCS;
    EXPECT_TRUE(info.hasPostings());
}

TEST_F(FieldInfoTest, HasFreqs) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.indexOptions = IndexOptions::DOCS;
    EXPECT_FALSE(info.hasFreqs());

    info.indexOptions = IndexOptions::DOCS_AND_FREQS;
    EXPECT_TRUE(info.hasFreqs());

    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    EXPECT_TRUE(info.hasFreqs());
}

TEST_F(FieldInfoTest, HasPositions) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.indexOptions = IndexOptions::DOCS_AND_FREQS;
    EXPECT_FALSE(info.hasPositions());

    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    EXPECT_TRUE(info.hasPositions());

    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
    EXPECT_TRUE(info.hasPositions());
}

TEST_F(FieldInfoTest, HasOffsets) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    EXPECT_FALSE(info.hasOffsets());

    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
    EXPECT_TRUE(info.hasOffsets());
}

TEST_F(FieldInfoTest, HasNorms) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    // No postings = no norms
    info.indexOptions = IndexOptions::NONE;
    info.omitNorms = false;
    EXPECT_FALSE(info.hasNorms());

    // Postings but omitNorms = no norms
    info.indexOptions = IndexOptions::DOCS;
    info.omitNorms = true;
    EXPECT_FALSE(info.hasNorms());

    // Postings and !omitNorms = has norms
    info.indexOptions = IndexOptions::DOCS;
    info.omitNorms = false;
    EXPECT_TRUE(info.hasNorms());
}

TEST_F(FieldInfoTest, HasDocValues) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.docValuesType = DocValuesType::NONE;
    EXPECT_FALSE(info.hasDocValues());

    info.docValuesType = DocValuesType::NUMERIC;
    EXPECT_TRUE(info.hasDocValues());
}

TEST_F(FieldInfoTest, HasPointValues) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.pointDimensionCount = 0;
    EXPECT_FALSE(info.hasPointValues());

    info.pointDimensionCount = 2;
    EXPECT_TRUE(info.hasPointValues());
}

// ==================== FieldInfo Attributes ====================

TEST_F(FieldInfoTest, AttributeGetSet) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    EXPECT_FALSE(info.getAttribute("key1").has_value());

    info.putAttribute("key1", "value1");
    auto attr = info.getAttribute("key1");
    ASSERT_TRUE(attr.has_value());
    EXPECT_EQ("value1", attr.value());

    info.putAttribute("key2", "value2");
    EXPECT_EQ("value1", info.getAttribute("key1").value());
    EXPECT_EQ("value2", info.getAttribute("key2").value());
}

TEST_F(FieldInfoTest, AttributeOverwrite) {
    FieldInfo info;
    info.name = "field";
    info.number = 0;

    info.putAttribute("key", "value1");
    EXPECT_EQ("value1", info.getAttribute("key").value());

    info.putAttribute("key", "value2");
    EXPECT_EQ("value2", info.getAttribute("key").value());
}

// ==================== FieldInfos Tests ====================

TEST_F(FieldInfoTest, FieldInfosConstruction) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "field1";
    info1.number = 0;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field2";
    info2.number = 1;
    info2.indexOptions = IndexOptions::DOCS_AND_FREQS;
    infos.push_back(info2);

    FieldInfos fieldInfos(std::move(infos));
    EXPECT_EQ(2, fieldInfos.size());
}

TEST_F(FieldInfoTest, FieldInfosLookupByName) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "title";
    info1.number = 0;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "body";
    info2.number = 1;
    infos.push_back(info2);

    FieldInfos fieldInfos(std::move(infos));

    const FieldInfo* found = fieldInfos.fieldInfo("title");
    ASSERT_NE(nullptr, found);
    EXPECT_EQ("title", found->name);
    EXPECT_EQ(0, found->number);

    found = fieldInfos.fieldInfo("body");
    ASSERT_NE(nullptr, found);
    EXPECT_EQ("body", found->name);
    EXPECT_EQ(1, found->number);

    found = fieldInfos.fieldInfo("nonexistent");
    EXPECT_EQ(nullptr, found);
}

TEST_F(FieldInfoTest, FieldInfosLookupByNumber) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "field1";
    info1.number = 0;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field2";
    info2.number = 1;
    infos.push_back(info2);

    FieldInfos fieldInfos(std::move(infos));

    const FieldInfo* found = fieldInfos.fieldInfo(0);
    ASSERT_NE(nullptr, found);
    EXPECT_EQ("field1", found->name);

    found = fieldInfos.fieldInfo(1);
    ASSERT_NE(nullptr, found);
    EXPECT_EQ("field2", found->name);

    found = fieldInfos.fieldInfo(2);
    EXPECT_EQ(nullptr, found);

    found = fieldInfos.fieldInfo(-1);
    EXPECT_EQ(nullptr, found);
}

TEST_F(FieldInfoTest, FieldInfosDuplicateName) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "field";
    info1.number = 0;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field";  // Duplicate!
    info2.number = 1;
    infos.push_back(info2);

    EXPECT_THROW(FieldInfos fieldInfos(std::move(infos)), std::invalid_argument);
}

TEST_F(FieldInfoTest, FieldInfosAggregateFlags) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "field1";
    info1.number = 0;
    info1.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    info1.storePayloads = true;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field2";
    info2.number = 1;
    info2.docValuesType = DocValuesType::NUMERIC;
    info2.pointDimensionCount = 2;
    info2.pointIndexDimensionCount = 2;
    info2.pointNumBytes = 4;
    infos.push_back(info2);

    FieldInfo info3;
    info3.name = "field3";
    info3.number = 2;
    info3.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
    info3.storeTermVector = true;
    infos.push_back(info3);

    FieldInfos fieldInfos(std::move(infos));

    EXPECT_TRUE(fieldInfos.hasFreq());
    EXPECT_TRUE(fieldInfos.hasPostings());
    EXPECT_TRUE(fieldInfos.hasProx());
    EXPECT_TRUE(fieldInfos.hasPayloads());
    EXPECT_TRUE(fieldInfos.hasOffsets());
    EXPECT_TRUE(fieldInfos.hasTermVectors());
    EXPECT_TRUE(fieldInfos.hasDocValues());
    EXPECT_TRUE(fieldInfos.hasPointValues());
}

TEST_F(FieldInfoTest, FieldInfosNoAggregateFlags) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "field1";
    info1.number = 0;
    infos.push_back(info1);

    FieldInfos fieldInfos(std::move(infos));

    EXPECT_FALSE(fieldInfos.hasFreq());
    EXPECT_FALSE(fieldInfos.hasPostings());
    EXPECT_FALSE(fieldInfos.hasProx());
    EXPECT_FALSE(fieldInfos.hasPayloads());
    EXPECT_FALSE(fieldInfos.hasOffsets());
    EXPECT_FALSE(fieldInfos.hasTermVectors());
    EXPECT_FALSE(fieldInfos.hasDocValues());
    EXPECT_FALSE(fieldInfos.hasPointValues());
}

TEST_F(FieldInfoTest, FieldInfosSoftDeletesField) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "deleted";
    info1.number = 0;
    info1.softDeletesField = true;
    info1.docValuesType = DocValuesType::NUMERIC;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field2";
    info2.number = 1;
    infos.push_back(info2);

    FieldInfos fieldInfos(std::move(infos));
    EXPECT_EQ("deleted", fieldInfos.getSoftDeletesField());
}

TEST_F(FieldInfoTest, FieldInfosParentField) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "parent";
    info1.number = 0;
    info1.isParentField = true;
    info1.docValuesType = DocValuesType::NUMERIC;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field2";
    info2.number = 1;
    infos.push_back(info2);

    FieldInfos fieldInfos(std::move(infos));
    EXPECT_EQ("parent", fieldInfos.getParentField());
}

TEST_F(FieldInfoTest, FieldInfosMultipleSoftDeletesFields) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "deleted1";
    info1.number = 0;
    info1.softDeletesField = true;
    info1.docValuesType = DocValuesType::NUMERIC;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "deleted2";
    info2.number = 1;
    info2.softDeletesField = true;
    info2.docValuesType = DocValuesType::NUMERIC;
    infos.push_back(info2);

    EXPECT_THROW(FieldInfos fieldInfos(std::move(infos)), std::invalid_argument);
}

TEST_F(FieldInfoTest, FieldInfosMultipleParentFields) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "parent1";
    info1.number = 0;
    info1.isParentField = true;
    info1.docValuesType = DocValuesType::NUMERIC;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "parent2";
    info2.number = 1;
    info2.isParentField = true;
    info2.docValuesType = DocValuesType::NUMERIC;
    infos.push_back(info2);

    EXPECT_THROW(FieldInfos fieldInfos(std::move(infos)), std::invalid_argument);
}

TEST_F(FieldInfoTest, FieldInfosIteration) {
    std::vector<FieldInfo> infos;

    FieldInfo info1;
    info1.name = "field0";
    info1.number = 0;
    infos.push_back(info1);

    FieldInfo info2;
    info2.name = "field1";
    info2.number = 1;
    infos.push_back(info2);

    FieldInfo info3;
    info3.name = "field2";
    info3.number = 2;
    infos.push_back(info3);

    FieldInfos fieldInfos(std::move(infos));

    int count = 0;
    for (const auto& info : fieldInfos) {
        EXPECT_EQ(count, info.number);
        count++;
    }
    EXPECT_EQ(3, count);
}

// ==================== FieldInfosBuilder Tests ====================

TEST_F(FieldInfoTest, BuilderGetOrAdd) {
    FieldInfosBuilder builder;

    int num1 = builder.getOrAdd("field1");
    EXPECT_EQ(0, num1);

    int num2 = builder.getOrAdd("field2");
    EXPECT_EQ(1, num2);

    // Adding same field returns existing number
    int num1Again = builder.getOrAdd("field1");
    EXPECT_EQ(0, num1Again);
}

TEST_F(FieldInfoTest, BuilderGetFieldInfo) {
    FieldInfosBuilder builder;

    builder.getOrAdd("field1");

    FieldInfo* info = builder.getFieldInfo("field1");
    ASSERT_NE(nullptr, info);
    EXPECT_EQ("field1", info->name);
    EXPECT_EQ(0, info->number);

    FieldInfo* notFound = builder.getFieldInfo("nonexistent");
    EXPECT_EQ(nullptr, notFound);
}

TEST_F(FieldInfoTest, BuilderUpdateIndexOptions) {
    FieldInfosBuilder builder;

    builder.getOrAdd("field1");

    // Upgrade from NONE to DOCS
    builder.updateIndexOptions("field1", IndexOptions::DOCS);
    FieldInfo* info = builder.getFieldInfo("field1");
    EXPECT_EQ(IndexOptions::DOCS, info->indexOptions);

    // Upgrade from DOCS to DOCS_AND_FREQS
    builder.updateIndexOptions("field1", IndexOptions::DOCS_AND_FREQS);
    EXPECT_EQ(IndexOptions::DOCS_AND_FREQS, info->indexOptions);

    // Try to downgrade (should be ignored)
    builder.updateIndexOptions("field1", IndexOptions::DOCS);
    EXPECT_EQ(IndexOptions::DOCS_AND_FREQS, info->indexOptions);
}

TEST_F(FieldInfoTest, BuilderUpdateNonExistentField) {
    FieldInfosBuilder builder;

    EXPECT_THROW(builder.updateIndexOptions("nonexistent", IndexOptions::DOCS),
                 std::invalid_argument);
}

TEST_F(FieldInfoTest, BuilderFinish) {
    FieldInfosBuilder builder;

    builder.getOrAdd("field2");
    builder.getOrAdd("field0");
    builder.getOrAdd("field1");

    builder.updateIndexOptions("field1", IndexOptions::DOCS_AND_FREQS);

    auto fieldInfos = builder.finish();

    EXPECT_EQ(3, fieldInfos->size());

    // Should be sorted by field number
    const FieldInfo* info0 = fieldInfos->fieldInfo(0);
    const FieldInfo* info1 = fieldInfos->fieldInfo(1);
    const FieldInfo* info2 = fieldInfos->fieldInfo(2);

    ASSERT_NE(nullptr, info0);
    ASSERT_NE(nullptr, info1);
    ASSERT_NE(nullptr, info2);

    EXPECT_EQ("field2", info0->name);
    EXPECT_EQ("field0", info1->name);
    EXPECT_EQ("field1", info2->name);
    EXPECT_EQ(IndexOptions::DOCS_AND_FREQS, info2->indexOptions);
}

TEST_F(FieldInfoTest, BuilderComplexScenario) {
    FieldInfosBuilder builder;

    // Add text field with positions
    builder.getOrAdd("title");
    builder.updateIndexOptions("title", IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    FieldInfo* titleInfo = builder.getFieldInfo("title");
    titleInfo->omitNorms = false;

    // Add date field with doc values
    builder.getOrAdd("date");
    builder.updateIndexOptions("date", IndexOptions::DOCS);
    FieldInfo* dateInfo = builder.getFieldInfo("date");
    dateInfo->docValuesType = DocValuesType::NUMERIC;
    dateInfo->omitNorms = true;

    // Add point field
    builder.getOrAdd("location");
    FieldInfo* locInfo = builder.getFieldInfo("location");
    locInfo->pointDimensionCount = 2;
    locInfo->pointIndexDimensionCount = 2;
    locInfo->pointNumBytes = 4;

    // Build and validate
    auto fieldInfos = builder.finish();

    EXPECT_EQ(3, fieldInfos->size());
    EXPECT_TRUE(fieldInfos->hasPostings());
    EXPECT_TRUE(fieldInfos->hasFreq());
    EXPECT_TRUE(fieldInfos->hasProx());
    EXPECT_TRUE(fieldInfos->hasDocValues());
    EXPECT_TRUE(fieldInfos->hasPointValues());
}

TEST_F(FieldInfoTest, FieldNumberAllocation) {
    FieldInfosBuilder builder;

    // Field numbers should be allocated sequentially
    EXPECT_EQ(0, builder.getOrAdd("field0"));
    EXPECT_EQ(1, builder.getOrAdd("field1"));
    EXPECT_EQ(2, builder.getOrAdd("field2"));
    EXPECT_EQ(3, builder.getOrAdd("field3"));
    EXPECT_EQ(4, builder.getOrAdd("field4"));

    // Re-adding should return existing numbers
    EXPECT_EQ(2, builder.getOrAdd("field2"));
    EXPECT_EQ(0, builder.getOrAdd("field0"));
}

// ==================== Point Values Tests ====================

TEST_F(FieldInfoTest, PointValuesValid) {
    FieldInfo info;
    info.name = "location";
    info.number = 0;
    info.pointDimensionCount = 2;
    info.pointIndexDimensionCount = 2;
    info.pointNumBytes = 8;

    EXPECT_NO_THROW(info.validate());
    EXPECT_TRUE(info.hasPointValues());
}

TEST_F(FieldInfoTest, PointValuesPartialIndexing) {
    FieldInfo info;
    info.name = "geo";
    info.number = 0;
    info.pointDimensionCount = 3;       // 3 dimensions stored
    info.pointIndexDimensionCount = 2;  // But only 2 indexed
    info.pointNumBytes = 4;

    EXPECT_NO_THROW(info.validate());
}

// ==================== Doc Values Skip Index Tests ====================

TEST_F(FieldInfoTest, DocValuesSkipIndexNumeric) {
    FieldInfo info;
    info.name = "count";
    info.number = 0;
    info.docValuesType = DocValuesType::NUMERIC;
    info.docValuesSkipIndex = DocValuesSkipIndexType::RANGE;

    EXPECT_NO_THROW(info.validate());
}

TEST_F(FieldInfoTest, DocValuesSkipIndexSorted) {
    FieldInfo info;
    info.name = "category";
    info.number = 0;
    info.docValuesType = DocValuesType::SORTED;
    info.docValuesSkipIndex = DocValuesSkipIndexType::RANGE;

    EXPECT_NO_THROW(info.validate());
}

TEST_F(FieldInfoTest, DocValuesSkipIndexIncompatible) {
    FieldInfo info;
    info.name = "data";
    info.number = 0;
    info.docValuesType = DocValuesType::NONE;
    info.docValuesSkipIndex = DocValuesSkipIndexType::RANGE;

    EXPECT_THROW(info.validate(), std::invalid_argument);
}
