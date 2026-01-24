// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/granularity/GranularityConfig.h"
#include "diagon/granularity/IMergeTreeIndexGranularity.h"
#include "diagon/granularity/MarkInCompressedFile.h"
#include "diagon/granularity/MarkRange.h"
#include "diagon/granularity/MergeTreeIndexGranularityAdaptive.h"
#include "diagon/granularity/MergeTreeIndexGranularityConstant.h"

#include <gtest/gtest.h>

using namespace diagon::granularity;

// ==================== MergeTreeIndexGranularityConstant Tests ====================

TEST(MergeTreeIndexGranularityConstantTest, Construction) {
    MergeTreeIndexGranularityConstant granularity(8192);

    EXPECT_EQ(8192, granularity.getGranularity());
    EXPECT_EQ(0, granularity.getMarksCount());
    EXPECT_EQ(0, granularity.getTotalRows());
    EXPECT_TRUE(granularity.empty());
}

TEST(MergeTreeIndexGranularityConstantTest, ConstructionWithMarks) {
    MergeTreeIndexGranularityConstant granularity(8192, 5);

    EXPECT_EQ(5, granularity.getMarksCount());
    EXPECT_FALSE(granularity.empty());
}

TEST(MergeTreeIndexGranularityConstantTest, AddMarks) {
    MergeTreeIndexGranularityConstant granularity(8192);

    granularity.addMark(8192);
    EXPECT_EQ(1, granularity.getMarksCount());
    EXPECT_EQ(8192, granularity.getTotalRows());

    granularity.addMark(8192);
    EXPECT_EQ(2, granularity.getMarksCount());
    EXPECT_EQ(16384, granularity.getTotalRows());

    granularity.addMark(8192);
    EXPECT_EQ(3, granularity.getMarksCount());
    EXPECT_EQ(24576, granularity.getTotalRows());
}

TEST(MergeTreeIndexGranularityConstantTest, AddPartialLastMark) {
    MergeTreeIndexGranularityConstant granularity(8192);

    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(5000);  // Note: rows parameter is ignored in constant granularity

    EXPECT_EQ(3, granularity.getMarksCount());
    EXPECT_EQ(24576, granularity.getTotalRows());  // 3 * 8192
}

TEST(MergeTreeIndexGranularityConstantTest, GetMarkRows) {
    MergeTreeIndexGranularityConstant granularity(8192);

    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(5000);

    // Constant granularity always returns granularity_ for all marks
    EXPECT_EQ(8192, granularity.getMarkRows(0));
    EXPECT_EQ(8192, granularity.getMarkRows(1));
    EXPECT_EQ(8192, granularity.getMarkRows(2));
}

TEST(MergeTreeIndexGranularityConstantTest, GetMarkRowsOutOfRange) {
    MergeTreeIndexGranularityConstant granularity(8192, 2);

    EXPECT_THROW(granularity.getMarkRows(2), std::out_of_range);
}

TEST(MergeTreeIndexGranularityConstantTest, GetRowsCountInRange) {
    MergeTreeIndexGranularityConstant granularity(8192);

    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(5000);

    EXPECT_EQ(8192, granularity.getRowsCountInRange(0, 1));
    EXPECT_EQ(16384, granularity.getRowsCountInRange(0, 2));
    EXPECT_EQ(24576, granularity.getRowsCountInRange(0, 3));
    EXPECT_EQ(32768, granularity.getRowsCountInRange(0, 4));  // All 4 marks * 8192

    EXPECT_EQ(8192, granularity.getRowsCountInRange(1, 2));
    EXPECT_EQ(24576, granularity.getRowsCountInRange(1, 4));  // 3 marks * 8192

    EXPECT_EQ(0, granularity.getRowsCountInRange(2, 2));
    EXPECT_EQ(0, granularity.getRowsCountInRange(3, 2));
}

TEST(MergeTreeIndexGranularityConstantTest, GetMarkContainingRow) {
    MergeTreeIndexGranularityConstant granularity(8192);

    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(5000);

    EXPECT_EQ(0, granularity.getMarkContainingRow(0));
    EXPECT_EQ(0, granularity.getMarkContainingRow(100));
    EXPECT_EQ(0, granularity.getMarkContainingRow(8191));
    EXPECT_EQ(1, granularity.getMarkContainingRow(8192));
    EXPECT_EQ(1, granularity.getMarkContainingRow(10000));
    EXPECT_EQ(2, granularity.getMarkContainingRow(16384));
    EXPECT_EQ(2, granularity.getMarkContainingRow(20000));

    // Row 24576 and beyond would be in mark 3, but we only have 3 marks (0-2)
    EXPECT_THROW(granularity.getMarkContainingRow(24576), std::out_of_range);
}

TEST(MergeTreeIndexGranularityConstantTest, CountMarksForRows) {
    MergeTreeIndexGranularityConstant granularity(8192);

    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(8192);

    EXPECT_EQ(1, granularity.countMarksForRows(0, 8192));
    EXPECT_EQ(2, granularity.countMarksForRows(0, 16384));
    EXPECT_EQ(2, granularity.countMarksForRows(0, 10000));
    EXPECT_EQ(1, granularity.countMarksForRows(1, 8192));
    EXPECT_EQ(2, granularity.countMarksForRows(1, 16384));
}

TEST(MergeTreeIndexGranularityConstantTest, HasFinalMark) {
    MergeTreeIndexGranularityConstant granularity(8192);

    EXPECT_FALSE(granularity.hasFinalMark());

    granularity.addMark(8192);
    EXPECT_FALSE(granularity.hasFinalMark());
}

// ==================== MergeTreeIndexGranularityAdaptive Tests ====================

TEST(MergeTreeIndexGranularityAdaptiveTest, Construction) {
    MergeTreeIndexGranularityAdaptive granularity;

    EXPECT_EQ(0, granularity.getMarksCount());
    EXPECT_EQ(0, granularity.getTotalRows());
    EXPECT_TRUE(granularity.empty());
}

TEST(MergeTreeIndexGranularityAdaptiveTest, AddMarks) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);
    EXPECT_EQ(1, granularity.getMarksCount());
    EXPECT_EQ(100, granularity.getTotalRows());
    EXPECT_EQ(100, granularity.getCumulativeRows(0));

    granularity.addMark(150);
    EXPECT_EQ(2, granularity.getMarksCount());
    EXPECT_EQ(250, granularity.getTotalRows());
    EXPECT_EQ(250, granularity.getCumulativeRows(1));

    granularity.addMark(200);
    EXPECT_EQ(3, granularity.getMarksCount());
    EXPECT_EQ(450, granularity.getTotalRows());
    EXPECT_EQ(450, granularity.getCumulativeRows(2));
}

TEST(MergeTreeIndexGranularityAdaptiveTest, GetMarkRows) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);
    granularity.addMark(150);
    granularity.addMark(200);

    EXPECT_EQ(100, granularity.getMarkRows(0));
    EXPECT_EQ(150, granularity.getMarkRows(1));
    EXPECT_EQ(200, granularity.getMarkRows(2));
}

TEST(MergeTreeIndexGranularityAdaptiveTest, GetMarkRowsOutOfRange) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);

    EXPECT_THROW(granularity.getMarkRows(1), std::out_of_range);
}

TEST(MergeTreeIndexGranularityAdaptiveTest, GetRowsCountInRange) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);
    granularity.addMark(150);
    granularity.addMark(200);
    granularity.addMark(50);

    EXPECT_EQ(100, granularity.getRowsCountInRange(0, 1));
    EXPECT_EQ(250, granularity.getRowsCountInRange(0, 2));
    EXPECT_EQ(450, granularity.getRowsCountInRange(0, 3));
    EXPECT_EQ(500, granularity.getRowsCountInRange(0, 4));

    EXPECT_EQ(150, granularity.getRowsCountInRange(1, 2));
    EXPECT_EQ(350, granularity.getRowsCountInRange(1, 3));
    EXPECT_EQ(400, granularity.getRowsCountInRange(1, 4));

    EXPECT_EQ(200, granularity.getRowsCountInRange(2, 3));
    EXPECT_EQ(250, granularity.getRowsCountInRange(2, 4));

    EXPECT_EQ(0, granularity.getRowsCountInRange(2, 2));
    EXPECT_EQ(0, granularity.getRowsCountInRange(3, 2));
}

TEST(MergeTreeIndexGranularityAdaptiveTest, GetMarkContainingRow) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);
    granularity.addMark(150);
    granularity.addMark(200);

    EXPECT_EQ(0, granularity.getMarkContainingRow(0));
    EXPECT_EQ(0, granularity.getMarkContainingRow(50));
    EXPECT_EQ(0, granularity.getMarkContainingRow(99));
    EXPECT_EQ(1, granularity.getMarkContainingRow(100));
    EXPECT_EQ(1, granularity.getMarkContainingRow(200));
    EXPECT_EQ(1, granularity.getMarkContainingRow(249));
    EXPECT_EQ(2, granularity.getMarkContainingRow(250));
    EXPECT_EQ(2, granularity.getMarkContainingRow(400));
}

TEST(MergeTreeIndexGranularityAdaptiveTest, GetMarkContainingRowOutOfRange) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);

    EXPECT_THROW(granularity.getMarkContainingRow(100), std::out_of_range);
}

TEST(MergeTreeIndexGranularityAdaptiveTest, CountMarksForRows) {
    MergeTreeIndexGranularityAdaptive granularity;

    granularity.addMark(100);
    granularity.addMark(150);
    granularity.addMark(200);

    EXPECT_EQ(1, granularity.countMarksForRows(0, 100));
    EXPECT_EQ(2, granularity.countMarksForRows(0, 250));
    EXPECT_EQ(3, granularity.countMarksForRows(0, 450));
    EXPECT_EQ(2, granularity.countMarksForRows(0, 200));

    EXPECT_EQ(1, granularity.countMarksForRows(1, 150));
    EXPECT_EQ(2, granularity.countMarksForRows(1, 350));
}

TEST(MergeTreeIndexGranularityAdaptiveTest, HasFinalMark) {
    MergeTreeIndexGranularityAdaptive granularity;

    EXPECT_FALSE(granularity.hasFinalMark());

    granularity.addMark(100);
    EXPECT_FALSE(granularity.hasFinalMark());

    granularity.addMark(0);
    EXPECT_TRUE(granularity.hasFinalMark());
}

// ==================== MarkInCompressedFile Tests ====================

TEST(MarkInCompressedFileTest, Construction) {
    MarkInCompressedFile mark;

    EXPECT_EQ(0, mark.offset_in_compressed_file);
    EXPECT_EQ(0, mark.offset_in_decompressed_block);
}

TEST(MarkInCompressedFileTest, ConstructionWithValues) {
    MarkInCompressedFile mark(1000, 500);

    EXPECT_EQ(1000, mark.offset_in_compressed_file);
    EXPECT_EQ(500, mark.offset_in_decompressed_block);
}

TEST(MarkInCompressedFileTest, Equality) {
    MarkInCompressedFile mark1(1000, 500);
    MarkInCompressedFile mark2(1000, 500);
    MarkInCompressedFile mark3(1000, 600);
    MarkInCompressedFile mark4(2000, 500);

    EXPECT_EQ(mark1, mark2);
    EXPECT_NE(mark1, mark3);
    EXPECT_NE(mark1, mark4);
}

// ==================== MarkRange Tests ====================

TEST(MarkRangeTest, Construction) {
    MarkRange range;

    EXPECT_EQ(0, range.begin);
    EXPECT_EQ(0, range.end);
    EXPECT_TRUE(range.empty());
}

TEST(MarkRangeTest, ConstructionWithValues) {
    MarkRange range(10, 20);

    EXPECT_EQ(10, range.begin);
    EXPECT_EQ(20, range.end);
    EXPECT_FALSE(range.empty());
    EXPECT_EQ(10, range.getNumberOfMarks());
}

TEST(MarkRangeTest, Empty) {
    MarkRange range1(10, 10);
    EXPECT_TRUE(range1.empty());
    EXPECT_EQ(0, range1.getNumberOfMarks());

    MarkRange range2(10, 5);
    EXPECT_TRUE(range2.empty());
    EXPECT_EQ(0, range2.getNumberOfMarks());
}

TEST(MarkRangeTest, Equality) {
    MarkRange range1(10, 20);
    MarkRange range2(10, 20);
    MarkRange range3(10, 25);
    MarkRange range4(5, 20);

    EXPECT_EQ(range1, range2);
    EXPECT_NE(range1, range3);
    EXPECT_NE(range1, range4);
}

TEST(MarkRangeTest, Comparison) {
    MarkRange range1(5, 10);
    MarkRange range2(10, 15);
    MarkRange range3(5, 15);

    EXPECT_TRUE(range1 < range2);
    EXPECT_TRUE(range1 < range3);
    EXPECT_FALSE(range2 < range1);
}

TEST(MarkRangeTest, MarkRangesToRows) {
    MergeTreeIndexGranularityConstant granularity(8192);
    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(8192);
    granularity.addMark(5000);

    MarkRanges ranges = {MarkRange(0, 1), MarkRange(1, 3), MarkRange(3, 4)};

    auto row_ranges = markRangesToRows(ranges, granularity);

    ASSERT_EQ(3, row_ranges.size());
    EXPECT_EQ(0, row_ranges[0].first);
    EXPECT_EQ(8192, row_ranges[0].second);
    EXPECT_EQ(8192, row_ranges[1].first);
    EXPECT_EQ(24576, row_ranges[1].second);
    EXPECT_EQ(24576, row_ranges[2].first);
    EXPECT_EQ(32768, row_ranges[2].second);  // Mark 3 also has 8192 rows in constant granularity
}

TEST(MarkRangeTest, MarkRangesToRowsEmptyRanges) {
    MergeTreeIndexGranularityConstant granularity(8192, 3);

    MarkRanges ranges = {MarkRange(0, 0), MarkRange(1, 1)};

    auto row_ranges = markRangesToRows(ranges, granularity);

    EXPECT_EQ(0, row_ranges.size());
}

// ==================== GranularityConfig Tests ====================

TEST(GranularityConfigTest, DefaultConstruction) {
    GranularityConfig config;

    EXPECT_EQ(8192, config.index_granularity);
    EXPECT_EQ(10 * 1024 * 1024, config.index_granularity_bytes);
    EXPECT_EQ(1024, config.min_index_granularity_bytes);
    EXPECT_TRUE(config.use_adaptive_granularity());
}

TEST(GranularityConfigTest, CreateAdaptiveGranularity) {
    GranularityConfig config;
    config.index_granularity_bytes = 10 * 1024 * 1024;

    auto granularity = config.createGranularity();

    ASSERT_NE(nullptr, granularity);
    EXPECT_TRUE(dynamic_cast<MergeTreeIndexGranularityAdaptive*>(granularity.get()) != nullptr);
}

TEST(GranularityConfigTest, CreateConstantGranularity) {
    GranularityConfig config;
    config.index_granularity_bytes = 0;

    auto granularity = config.createGranularity();

    ASSERT_NE(nullptr, granularity);
    EXPECT_TRUE(dynamic_cast<MergeTreeIndexGranularityConstant*>(granularity.get()) != nullptr);
}

// ==================== GranuleWriter Tests ====================

TEST(GranuleWriterTest, ConstructionWithConstantGranularity) {
    GranularityConfig config;
    config.index_granularity = 8192;
    config.index_granularity_bytes = 0;

    GranuleWriter writer(config);

    EXPECT_EQ(0, writer.getGranularity().getMarksCount());
}

TEST(GranuleWriterTest, ConstructionWithAdaptiveGranularity) {
    GranularityConfig config;
    config.index_granularity = 8192;
    config.index_granularity_bytes = 10 * 1024 * 1024;

    GranuleWriter writer(config);

    EXPECT_EQ(0, writer.getGranularity().getMarksCount());
}

TEST(GranuleWriterTest, ShouldFinishGranuleConstant) {
    GranularityConfig config;
    config.index_granularity = 8192;
    config.index_granularity_bytes = 0;

    GranuleWriter writer(config);

    EXPECT_FALSE(writer.shouldFinishGranule(100, 1000));
    EXPECT_FALSE(writer.shouldFinishGranule(8191, 1000000));
    EXPECT_TRUE(writer.shouldFinishGranule(8192, 1000));
}

TEST(GranuleWriterTest, ShouldFinishGranuleAdaptive) {
    GranularityConfig config;
    config.index_granularity = 8192;
    config.index_granularity_bytes = 10 * 1024 * 1024;

    GranuleWriter writer(config);

    EXPECT_FALSE(writer.shouldFinishGranule(100, 1000));
    EXPECT_FALSE(writer.shouldFinishGranule(8000, 5 * 1024 * 1024));
    EXPECT_TRUE(writer.shouldFinishGranule(8000, 10 * 1024 * 1024));
    EXPECT_TRUE(writer.shouldFinishGranule(8192, 5 * 1024 * 1024));
}

TEST(GranuleWriterTest, FinishGranule) {
    GranularityConfig config;
    config.index_granularity = 8192;
    config.index_granularity_bytes = 0;

    GranuleWriter writer(config);

    writer.finishGranule(8192);
    EXPECT_EQ(1, writer.getGranularity().getMarksCount());

    writer.finishGranule(8192);
    EXPECT_EQ(2, writer.getGranularity().getMarksCount());

    writer.finishGranule(5000);
    EXPECT_EQ(3, writer.getGranularity().getMarksCount());
}

TEST(GranuleWriterTest, FinishGranuleAdaptive) {
    GranularityConfig config;
    config.index_granularity = 8192;
    config.index_granularity_bytes = 10 * 1024 * 1024;

    GranuleWriter writer(config);

    writer.finishGranule(100);
    writer.finishGranule(200);
    writer.finishGranule(150);

    EXPECT_EQ(3, writer.getGranularity().getMarksCount());
    EXPECT_EQ(450, writer.getGranularity().getTotalRows());
}

TEST(GranuleWriterTest, GetGranularityPtr) {
    GranularityConfig config;
    config.index_granularity_bytes = 0;

    GranuleWriter writer(config);

    auto granularity = writer.getGranularityPtr();
    ASSERT_NE(nullptr, granularity);
    EXPECT_TRUE(dynamic_cast<MergeTreeIndexGranularityConstant*>(granularity.get()) != nullptr);
}
