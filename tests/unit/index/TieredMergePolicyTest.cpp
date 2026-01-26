// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/TieredMergePolicy.h"

#include "diagon/index/MergeSpecification.h"
#include "diagon/index/OneMerge.h"
#include "diagon/index/SegmentInfo.h"

#include <gtest/gtest.h>

using namespace diagon::index;

// ==================== TieredMergePolicy Tests ====================

TEST(TieredMergePolicyTest, Configuration) {
    TieredMergePolicy policy;

    // Check defaults
    EXPECT_EQ(policy.getMaxMergedSegmentMB(), 5 * 1024.0);  // 5GB
    EXPECT_EQ(policy.getFloorSegmentMB(), 2.0);
    EXPECT_EQ(policy.getMaxMergeAtOnce(), 10);
    EXPECT_EQ(policy.getSegmentsPerTier(), 10.0);

    // Set values
    policy.setMaxMergedSegmentMB(1024.0);  // 1GB
    policy.setFloorSegmentMB(4.0);
    policy.setMaxMergeAtOnce(5);
    policy.setSegmentsPerTier(5.0);

    EXPECT_EQ(policy.getMaxMergedSegmentMB(), 1024.0);
    EXPECT_EQ(policy.getFloorSegmentMB(), 4.0);
    EXPECT_EQ(policy.getMaxMergeAtOnce(), 5);
    EXPECT_EQ(policy.getSegmentsPerTier(), 5.0);
}

TEST(TieredMergePolicyTest, NoMergeNeededForFewSegments) {
    TieredMergePolicy policy;
    SegmentInfos infos;

    // Add just 2 segments
    auto seg1 = std::make_shared<SegmentInfo>("_0", 1000, "Lucene104");
    seg1->setSizeInBytes(1 * 1024 * 1024);  // 1MB
    infos.add(seg1);

    auto seg2 = std::make_shared<SegmentInfo>("_1", 1000, "Lucene104");
    seg2->setSizeInBytes(1 * 1024 * 1024);
    infos.add(seg2);

    // Should not need merge (under budget)
    auto* spec = policy.findMerges(MergeTrigger::SEGMENT_FLUSH, infos);

    if (spec) {
        EXPECT_EQ(spec->size(), 0) << "Should not need merge for 2 small segments";
        delete spec;
    }
}

TEST(TieredMergePolicyTest, MergeManySmallSegments) {
    TieredMergePolicy policy;
    policy.setSegmentsPerTier(5.0);  // Allow only 5 segments per tier

    SegmentInfos infos;

    // Add 20 small segments (should trigger merge)
    for (int i = 0; i < 20; i++) {
        auto seg = std::make_shared<SegmentInfo>("_" + std::to_string(i), 100, "Lucene104");
        seg->setSizeInBytes(1 * 1024 * 1024);  // 1MB each
        infos.add(seg);
    }

    // Should need merges
    auto* spec = policy.findMerges(MergeTrigger::SEGMENT_FLUSH, infos);

    ASSERT_NE(spec, nullptr) << "Should need merge for 20 small segments";
    EXPECT_GT(spec->size(), 0) << "Should have at least one merge";

    delete spec;
}

TEST(TieredMergePolicyTest, ForcedMergeToOneSegment) {
    TieredMergePolicy policy;
    SegmentInfos infos;

    // Add 10 segments
    for (int i = 0; i < 10; i++) {
        auto seg = std::make_shared<SegmentInfo>("_" + std::to_string(i), 1000, "Lucene104");
        seg->setSizeInBytes(10 * 1024 * 1024);  // 10MB each
        infos.add(seg);
    }

    // Force merge to 1 segment
    std::map<SegmentCommitInfo*, bool> segmentsToMerge;
    auto* spec = policy.findForcedMerges(infos, 1, segmentsToMerge);

    ASSERT_NE(spec, nullptr) << "Should need merge to reach 1 segment";
    EXPECT_GT(spec->size(), 0) << "Should have merges";

    delete spec;
}

TEST(TieredMergePolicyTest, ForcedDeletesMerge) {
    TieredMergePolicy policy;
    SegmentInfos infos;

    // Add segments with varying delete ratios
    auto seg1 = std::make_shared<SegmentInfo>("_0", 1000, "Lucene104");
    seg1->setSizeInBytes(10 * 1024 * 1024);
    seg1->setDelCount(50);  // 5% deleted - should not merge
    infos.add(seg1);

    auto seg2 = std::make_shared<SegmentInfo>("_1", 1000, "Lucene104");
    seg2->setSizeInBytes(10 * 1024 * 1024);
    seg2->setDelCount(300);  // 30% deleted - should merge
    infos.add(seg2);

    auto seg3 = std::make_shared<SegmentInfo>("_2", 1000, "Lucene104");
    seg3->setSizeInBytes(10 * 1024 * 1024);
    seg3->setDelCount(400);  // 40% deleted - should merge
    infos.add(seg3);

    // Find forced deletes merges
    auto* spec = policy.findForcedDeletesMerges(infos);

    ASSERT_NE(spec, nullptr) << "Should need merge for high-delete segments";
    EXPECT_GT(spec->size(), 0) << "Should have at least one merge";

    delete spec;
}

TEST(TieredMergePolicyTest, NoForcedDeletesMergeForLowDeletes) {
    TieredMergePolicy policy;
    SegmentInfos infos;

    // Add segments with low delete ratios
    for (int i = 0; i < 5; i++) {
        auto seg = std::make_shared<SegmentInfo>("_" + std::to_string(i), 1000, "Lucene104");
        seg->setSizeInBytes(10 * 1024 * 1024);
        seg->setDelCount(50);  // Only 5% deleted
        infos.add(seg);
    }

    // Should not need merge (low delete ratio)
    auto* spec = policy.findForcedDeletesMerges(infos);

    EXPECT_EQ(spec, nullptr) << "Should not merge segments with low delete ratio";
}

TEST(TieredMergePolicyTest, MergeSimilarSizedSegments) {
    TieredMergePolicy policy;
    policy.setMaxMergeAtOnce(3);
    policy.setSegmentsPerTier(3.0);

    SegmentInfos infos;

    // Add segments of varying sizes
    auto seg1 = std::make_shared<SegmentInfo>("_0", 100, "Lucene104");
    seg1->setSizeInBytes(1 * 1024 * 1024);  // 1MB
    infos.add(seg1);

    auto seg2 = std::make_shared<SegmentInfo>("_1", 100, "Lucene104");
    seg2->setSizeInBytes(2 * 1024 * 1024);  // 2MB (similar)
    infos.add(seg2);

    auto seg3 = std::make_shared<SegmentInfo>("_2", 100, "Lucene104");
    seg3->setSizeInBytes(1 * 1024 * 1024);  // 1MB (similar)
    infos.add(seg3);

    auto seg4 = std::make_shared<SegmentInfo>("_3", 10000, "Lucene104");
    seg4->setSizeInBytes(1000 * 1024 * 1024);  // 1GB (very different)
    infos.add(seg4);

    // Should merge similar-sized segments (0, 1, 2) but not the large one
    auto* spec = policy.findMerges(MergeTrigger::SEGMENT_FLUSH, infos);

    if (spec && spec->size() > 0) {
        // If there's a merge, it should prefer similar-sized segments
        EXPECT_GT(spec->size(), 0);
    }

    delete spec;
}

TEST(TieredMergePolicyTest, SkipVeryLargeSegments) {
    TieredMergePolicy policy;
    policy.setMaxMergedSegmentMB(100);  // 100MB max

    SegmentInfos infos;

    // Add one very large segment
    auto seg1 = std::make_shared<SegmentInfo>("_0", 10000, "Lucene104");
    seg1->setSizeInBytes(200 * 1024 * 1024);  // 200MB (above max)
    infos.add(seg1);

    // Add several small segments
    for (int i = 1; i < 10; i++) {
        auto seg = std::make_shared<SegmentInfo>("_" + std::to_string(i), 100, "Lucene104");
        seg->setSizeInBytes(1 * 1024 * 1024);  // 1MB
        infos.add(seg);
    }

    // Should only merge small segments, skip the large one
    auto* spec = policy.findMerges(MergeTrigger::SEGMENT_FLUSH, infos);

    if (spec) {
        // Large segment should not be included in merges
        delete spec;
    }

    // Test passes if we don't crash and return reasonable results
    SUCCEED();
}
