// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/skipindex/IMergeTreeIndex.h"
#include "diagon/index/skipindex/IMergeTreeIndexAggregator.h"
#include "diagon/index/skipindex/IMergeTreeIndexCondition.h"
#include "diagon/index/skipindex/IMergeTreeIndexGranule.h"
#include "diagon/index/skipindex/MergeTreeIndexMinMax.h"

#include <gtest/gtest.h>

using namespace diagon::index::skipindex;

// ==================== IndexType Tests ====================

TEST(IndexTypeTest, Values) {
    EXPECT_EQ(IndexType::MINMAX, IndexType::MINMAX);
    EXPECT_EQ(IndexType::SET, IndexType::SET);
    EXPECT_EQ(IndexType::BLOOM_FILTER, IndexType::BLOOM_FILTER);
    EXPECT_EQ(IndexType::NGRAMBF_V1, IndexType::NGRAMBF_V1);
}

// ==================== IndexDescription Tests ====================

TEST(IndexDescriptionTest, Construction) {
    IndexDescription desc("price_idx", IndexType::MINMAX, 4);

    EXPECT_EQ("price_idx", desc.name);
    EXPECT_EQ(IndexType::MINMAX, desc.type);
    EXPECT_EQ(4, desc.granularity);
}

TEST(IndexDescriptionTest, DefaultGranularity) {
    IndexDescription desc("category_idx", IndexType::SET);

    EXPECT_EQ("category_idx", desc.name);
    EXPECT_EQ(IndexType::SET, desc.type);
    EXPECT_EQ(1, desc.granularity);
}

// ==================== MergeTreeIndexVersion Tests ====================

TEST(MergeTreeIndexVersionTest, Constants) {
    EXPECT_EQ(1, MINMAX_VERSION_V1);
    EXPECT_EQ(2, MINMAX_VERSION_V2);
    EXPECT_EQ(1, SET_VERSION_V1);
    EXPECT_EQ(1, BLOOM_FILTER_VERSION_V1);
}

// ==================== MergeTreeIndexGranuleMinMax Tests ====================

TEST(MergeTreeIndexGranuleMinMaxTest, Construction) {
    MergeTreeIndexGranuleMinMax granule(1);

    // Initially empty until values are added
    EXPECT_TRUE(granule.empty());
    EXPECT_EQ(0, granule.memoryUsageBytes());
}

TEST(MergeTreeIndexGranuleMinMaxTest, AddValues) {
    MergeTreeIndexGranuleMinMax granule(1);

    granule.addMinValue(10.0);
    granule.addMaxValue(10.0);

    EXPECT_DOUBLE_EQ(10.0, granule.getMinValue());
    EXPECT_DOUBLE_EQ(10.0, granule.getMaxValue());
}

TEST(MergeTreeIndexGranuleMinMaxTest, MinMaxTracking) {
    MergeTreeIndexGranuleMinMax granule(1);

    // Add values in random order
    granule.addMinValue(50.0);
    granule.addMaxValue(50.0);

    granule.addMinValue(10.0);   // New min
    granule.addMaxValue(100.0);  // New max

    granule.addMinValue(30.0);  // Not a new min
    granule.addMaxValue(80.0);  // Not a new max

    EXPECT_DOUBLE_EQ(10.0, granule.getMinValue());
    EXPECT_DOUBLE_EQ(100.0, granule.getMaxValue());
}

TEST(MergeTreeIndexGranuleMinMaxTest, MemoryUsage) {
    MergeTreeIndexGranuleMinMax granule(1);

    EXPECT_EQ(0, granule.memoryUsageBytes());  // Empty initially

    // Add values
    granule.addMinValue(10.0);
    granule.addMaxValue(50.0);

    EXPECT_GT(granule.memoryUsageBytes(), 0);  // Now has data
}

// ==================== MergeTreeIndexAggregatorMinMax Tests ====================

TEST(MergeTreeIndexAggregatorMinMaxTest, Construction) {
    MergeTreeIndexAggregatorMinMax aggregator(1);

    // Initially has empty granule
    EXPECT_TRUE(aggregator.empty());
}

TEST(MergeTreeIndexAggregatorMinMaxTest, AddValue) {
    MergeTreeIndexAggregatorMinMax aggregator(1);

    aggregator.addValue(42.0);

    EXPECT_FALSE(aggregator.empty());
}

TEST(MergeTreeIndexAggregatorMinMaxTest, GetGranuleAndReset) {
    MergeTreeIndexAggregatorMinMax aggregator(1);

    aggregator.addValue(10.0);
    aggregator.addValue(50.0);
    aggregator.addValue(30.0);

    auto granule = aggregator.getGranuleAndReset();

    ASSERT_NE(nullptr, granule);
    auto minmax_granule = std::dynamic_pointer_cast<MergeTreeIndexGranuleMinMax>(granule);
    ASSERT_NE(nullptr, minmax_granule);

    EXPECT_DOUBLE_EQ(10.0, minmax_granule->getMinValue());
    EXPECT_DOUBLE_EQ(50.0, minmax_granule->getMaxValue());

    // After reset, aggregator should have new empty granule
    EXPECT_TRUE(aggregator.empty());
}

// ==================== MergeTreeIndexConditionMinMax Tests ====================

TEST(MergeTreeIndexConditionMinMaxTest, Construction) {
    MergeTreeIndexConditionMinMax condition;

    EXPECT_FALSE(condition.alwaysUnknownOrTrue());
    EXPECT_EQ("MinMax condition", condition.getDescription());
}

TEST(MergeTreeIndexConditionMinMaxTest, MayBeTrueOnGranuleOverlap) {
    MergeTreeIndexConditionMinMax condition;
    condition.setRange(20.0, 80.0);  // Looking for values in [20, 80]

    auto granule = std::make_shared<MergeTreeIndexGranuleMinMax>(1);
    granule->addMinValue(10.0);
    granule->addMaxValue(50.0);

    // Granule [10, 50] overlaps with condition [20, 80]
    EXPECT_TRUE(condition.mayBeTrueOnGranule(granule));
}

TEST(MergeTreeIndexConditionMinMaxTest, MayBeTrueOnGranuleTooLow) {
    MergeTreeIndexConditionMinMax condition;
    condition.setRange(50.0, 100.0);  // Looking for values in [50, 100]

    auto granule = std::make_shared<MergeTreeIndexGranuleMinMax>(1);
    granule->addMinValue(10.0);
    granule->addMaxValue(30.0);

    // Granule [10, 30] is below condition [50, 100]
    EXPECT_FALSE(condition.mayBeTrueOnGranule(granule));
}

TEST(MergeTreeIndexConditionMinMaxTest, MayBeTrueOnGranuleTooHigh) {
    MergeTreeIndexConditionMinMax condition;
    condition.setRange(10.0, 50.0);  // Looking for values in [10, 50]

    auto granule = std::make_shared<MergeTreeIndexGranuleMinMax>(1);
    granule->addMinValue(60.0);
    granule->addMaxValue(100.0);

    // Granule [60, 100] is above condition [10, 50]
    EXPECT_FALSE(condition.mayBeTrueOnGranule(granule));
}

TEST(MergeTreeIndexConditionMinMaxTest, MayBeTrueOnGranuleContained) {
    MergeTreeIndexConditionMinMax condition;
    condition.setRange(0.0, 100.0);  // Looking for values in [0, 100]

    auto granule = std::make_shared<MergeTreeIndexGranuleMinMax>(1);
    granule->addMinValue(20.0);
    granule->addMaxValue(80.0);

    // Granule [20, 80] is fully contained in condition [0, 100]
    EXPECT_TRUE(condition.mayBeTrueOnGranule(granule));
}

TEST(MergeTreeIndexConditionMinMaxTest, MayBeTrueOnGranuleContains) {
    MergeTreeIndexConditionMinMax condition;
    condition.setRange(30.0, 70.0);  // Looking for values in [30, 70]

    auto granule = std::make_shared<MergeTreeIndexGranuleMinMax>(1);
    granule->addMinValue(0.0);
    granule->addMaxValue(100.0);

    // Granule [0, 100] fully contains condition [30, 70]
    EXPECT_TRUE(condition.mayBeTrueOnGranule(granule));
}

// ==================== MergeTreeIndexMinMax Tests ====================

TEST(MergeTreeIndexMinMaxTest, Construction) {
    IndexDescription desc("price", IndexType::MINMAX, 4);
    MergeTreeIndexMinMax index(desc);

    EXPECT_EQ("skp_idx_price", index.getFileName());
    EXPECT_EQ(".idx", index.getFileExtension());
    EXPECT_EQ(4, index.getGranularity());
    EXPECT_EQ("price", index.getName());
    EXPECT_EQ(IndexType::MINMAX, index.getType());
}

TEST(MergeTreeIndexMinMaxTest, CreateIndexGranule) {
    IndexDescription desc("price", IndexType::MINMAX);
    MergeTreeIndexMinMax index(desc);

    auto granule = index.createIndexGranule();

    ASSERT_NE(nullptr, granule);
    EXPECT_TRUE(granule->empty());  // Newly created granule is empty
}

TEST(MergeTreeIndexMinMaxTest, CreateIndexAggregator) {
    IndexDescription desc("price", IndexType::MINMAX);
    MergeTreeIndexMinMax index(desc);

    auto aggregator = index.createIndexAggregator();

    ASSERT_NE(nullptr, aggregator);
    EXPECT_TRUE(aggregator->empty());  // Newly created aggregator is empty
}

TEST(MergeTreeIndexMinMaxTest, CreateIndexCondition) {
    IndexDescription desc("price", IndexType::MINMAX);
    MergeTreeIndexMinMax index(desc);

    auto condition = index.createIndexCondition();

    ASSERT_NE(nullptr, condition);
    EXPECT_FALSE(condition->alwaysUnknownOrTrue());
}

// ==================== Integration Tests ====================

TEST(SkipIndexIntegrationTest, MinMaxFilteringWorkflow) {
    // 1. Create index
    IndexDescription desc("price", IndexType::MINMAX, 1);
    MergeTreeIndexMinMax index(desc);

    // 2. Build granule during write
    auto aggregator = std::dynamic_pointer_cast<MergeTreeIndexAggregatorMinMax>(
        index.createIndexAggregator());
    ASSERT_NE(nullptr, aggregator);

    aggregator->addValue(10.0);
    aggregator->addValue(25.0);
    aggregator->addValue(50.0);
    aggregator->addValue(75.0);
    aggregator->addValue(100.0);

    auto granule = aggregator->getGranuleAndReset();
    auto minmax_granule = std::dynamic_pointer_cast<MergeTreeIndexGranuleMinMax>(granule);
    ASSERT_NE(nullptr, minmax_granule);

    EXPECT_DOUBLE_EQ(10.0, minmax_granule->getMinValue());
    EXPECT_DOUBLE_EQ(100.0, minmax_granule->getMaxValue());

    // 3. Query time: Create condition for "WHERE price >= 50 AND price <= 80"
    auto condition = std::dynamic_pointer_cast<MergeTreeIndexConditionMinMax>(
        index.createIndexCondition());
    ASSERT_NE(nullptr, condition);

    condition->setRange(50.0, 80.0);

    // 4. Test granule filtering
    EXPECT_TRUE(condition->mayBeTrueOnGranule(granule));

    // 5. Test with non-matching range
    condition->setRange(150.0, 200.0);
    EXPECT_FALSE(condition->mayBeTrueOnGranule(granule));
}
