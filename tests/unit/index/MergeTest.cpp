// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/MergePolicy.h"
#include "diagon/index/MergeScheduler.h"
#include "diagon/index/MergeSpecification.h"
#include "diagon/index/OneMerge.h"
#include "diagon/index/TieredMergePolicy.h"

#include <gtest/gtest.h>

using namespace diagon::index;

// ==================== MergeTrigger Tests ====================

TEST(MergeTriggerTest, Values) {
    EXPECT_EQ(MergeTrigger::SEGMENT_FLUSH, MergeTrigger::SEGMENT_FLUSH);
    EXPECT_EQ(MergeTrigger::FULL_FLUSH, MergeTrigger::FULL_FLUSH);
    EXPECT_EQ(MergeTrigger::COMMIT, MergeTrigger::COMMIT);
    EXPECT_EQ(MergeTrigger::GET_READER, MergeTrigger::GET_READER);
    EXPECT_EQ(MergeTrigger::CLOSING, MergeTrigger::CLOSING);
    EXPECT_EQ(MergeTrigger::EXPLICIT, MergeTrigger::EXPLICIT);
}

// ==================== OneMerge Tests ====================

TEST(OneMergeTest, Construction) {
    std::vector<SegmentCommitInfo*> segments;
    OneMerge merge(segments);

    EXPECT_EQ(0, merge.getSegments().size());
    EXPECT_EQ(0, merge.getTotalDocCount());
    EXPECT_EQ(OneMerge::State::NOT_STARTED, merge.getState());
}

TEST(OneMergeTest, State) {
    std::vector<SegmentCommitInfo*> segments;
    OneMerge merge(segments);

    EXPECT_EQ(OneMerge::State::NOT_STARTED, merge.getState());
    EXPECT_FALSE(merge.isRunning());
    EXPECT_FALSE(merge.isAborted());

    merge.setState(OneMerge::State::RUNNING);
    EXPECT_EQ(OneMerge::State::RUNNING, merge.getState());
    EXPECT_TRUE(merge.isRunning());
    EXPECT_FALSE(merge.isAborted());

    merge.setState(OneMerge::State::ABORTED);
    EXPECT_EQ(OneMerge::State::ABORTED, merge.getState());
    EXPECT_FALSE(merge.isRunning());
    EXPECT_TRUE(merge.isAborted());

    merge.setState(OneMerge::State::COMPLETED);
    EXPECT_EQ(OneMerge::State::COMPLETED, merge.getState());
    EXPECT_FALSE(merge.isRunning());
    EXPECT_FALSE(merge.isAborted());
}

TEST(OneMergeTest, SegString) {
    std::vector<SegmentCommitInfo*> segments;
    OneMerge merge(segments);

    EXPECT_EQ("merge(0 segments)", merge.segString());
}

TEST(OneMergeTest, StateTransitions) {
    std::vector<SegmentCommitInfo*> segments;
    OneMerge merge(segments);

    // NOT_STARTED -> RUNNING
    merge.setState(OneMerge::State::RUNNING);
    EXPECT_EQ(OneMerge::State::RUNNING, merge.getState());

    // RUNNING -> PAUSED
    merge.setState(OneMerge::State::PAUSED);
    EXPECT_EQ(OneMerge::State::PAUSED, merge.getState());

    // PAUSED -> RUNNING
    merge.setState(OneMerge::State::RUNNING);
    EXPECT_EQ(OneMerge::State::RUNNING, merge.getState());

    // RUNNING -> COMPLETED
    merge.setState(OneMerge::State::COMPLETED);
    EXPECT_EQ(OneMerge::State::COMPLETED, merge.getState());
}

// ==================== MergeSpecification Tests ====================

TEST(MergeSpecificationTest, Construction) {
    MergeSpecification spec;

    EXPECT_EQ(0, spec.size());
    EXPECT_TRUE(spec.empty());
}

TEST(MergeSpecificationTest, AddMerge) {
    MergeSpecification spec;

    std::vector<SegmentCommitInfo*> segments1;
    auto merge1 = std::make_unique<OneMerge>(segments1);
    spec.add(std::move(merge1));

    EXPECT_EQ(1, spec.size());
    EXPECT_FALSE(spec.empty());

    std::vector<SegmentCommitInfo*> segments2;
    auto merge2 = std::make_unique<OneMerge>(segments2);
    spec.add(std::move(merge2));

    EXPECT_EQ(2, spec.size());
    EXPECT_FALSE(spec.empty());
}

TEST(MergeSpecificationTest, GetMerges) {
    MergeSpecification spec;

    std::vector<SegmentCommitInfo*> segments;
    auto merge = std::make_unique<OneMerge>(segments);
    spec.add(std::move(merge));

    const auto& merges = spec.getMerges();
    EXPECT_EQ(1, merges.size());
    ASSERT_NE(nullptr, merges[0]);
}

TEST(MergeSpecificationTest, SegString) {
    MergeSpecification spec;

    EXPECT_EQ("", spec.segString());

    std::vector<SegmentCommitInfo*> segments1;
    auto merge1 = std::make_unique<OneMerge>(segments1);
    spec.add(std::move(merge1));

    EXPECT_EQ("[merge 0]", spec.segString());

    std::vector<SegmentCommitInfo*> segments2;
    auto merge2 = std::make_unique<OneMerge>(segments2);
    spec.add(std::move(merge2));

    EXPECT_EQ("[merge 0] [merge 1]", spec.segString());
}

// ==================== TieredMergePolicy Tests ====================

TEST(TieredMergePolicyTest, DefaultConfiguration) {
    TieredMergePolicy policy;

    EXPECT_DOUBLE_EQ(5 * 1024.0, policy.getMaxMergedSegmentMB());
    EXPECT_DOUBLE_EQ(2.0, policy.getFloorSegmentMB());
    EXPECT_EQ(10, policy.getMaxMergeAtOnce());
    EXPECT_DOUBLE_EQ(10.0, policy.getSegmentsPerTier());
}

TEST(TieredMergePolicyTest, SetMaxMergedSegmentMB) {
    TieredMergePolicy policy;

    policy.setMaxMergedSegmentMB(1024.0);
    EXPECT_DOUBLE_EQ(1024.0, policy.getMaxMergedSegmentMB());

    policy.setMaxMergedSegmentMB(10 * 1024.0);
    EXPECT_DOUBLE_EQ(10 * 1024.0, policy.getMaxMergedSegmentMB());
}

TEST(TieredMergePolicyTest, SetFloorSegmentMB) {
    TieredMergePolicy policy;

    policy.setFloorSegmentMB(1.0);
    EXPECT_DOUBLE_EQ(1.0, policy.getFloorSegmentMB());

    policy.setFloorSegmentMB(5.0);
    EXPECT_DOUBLE_EQ(5.0, policy.getFloorSegmentMB());
}

TEST(TieredMergePolicyTest, SetMaxMergeAtOnce) {
    TieredMergePolicy policy;

    policy.setMaxMergeAtOnce(5);
    EXPECT_EQ(5, policy.getMaxMergeAtOnce());

    policy.setMaxMergeAtOnce(20);
    EXPECT_EQ(20, policy.getMaxMergeAtOnce());
}

TEST(TieredMergePolicyTest, SetSegmentsPerTier) {
    TieredMergePolicy policy;

    policy.setSegmentsPerTier(5.0);
    EXPECT_DOUBLE_EQ(5.0, policy.getSegmentsPerTier());

    policy.setSegmentsPerTier(15.0);
    EXPECT_DOUBLE_EQ(15.0, policy.getSegmentsPerTier());
}

TEST(TieredMergePolicyTest, KeepFullyDeletedSegment) {
    TieredMergePolicy policy;

    // Stub SegmentCommitInfo
    SegmentCommitInfo* info = nullptr;

    // Default: don't keep fully deleted segments
    EXPECT_FALSE(policy.keepFullyDeletedSegment(*info));
}

// ==================== MergePolicy Interface Tests ====================

TEST(MergePolicyTest, InterfaceCompiles) {
    // This test verifies that the MergePolicy interface compiles
    // and can be used polymorphically
    TieredMergePolicy policy;
    MergePolicy* base = &policy;

    EXPECT_DOUBLE_EQ(5 * 1024.0, base->getMaxMergedSegmentMB());
    EXPECT_DOUBLE_EQ(2.0, base->getFloorSegmentMB());

    base->setMaxMergedSegmentMB(2048.0);
    EXPECT_DOUBLE_EQ(2048.0, base->getMaxMergedSegmentMB());
}

// ==================== Integration Tests ====================

TEST(MergeIntegrationTest, CreateMergeSpecification) {
    MergeSpecification spec;

    // Create several merges
    for (int i = 0; i < 3; ++i) {
        std::vector<SegmentCommitInfo*> segments;
        auto merge = std::make_unique<OneMerge>(segments);
        merge->setState(OneMerge::State::NOT_STARTED);
        spec.add(std::move(merge));
    }

    EXPECT_EQ(3, spec.size());

    // Verify all merges are in NOT_STARTED state
    const auto& merges = spec.getMerges();
    for (const auto& merge : merges) {
        EXPECT_EQ(OneMerge::State::NOT_STARTED, merge->getState());
    }
}

TEST(MergeIntegrationTest, PolicyConfiguration) {
    TieredMergePolicy policy;

    // Configure policy
    policy.setMaxMergedSegmentMB(1024.0);
    policy.setFloorSegmentMB(1.0);
    policy.setMaxMergeAtOnce(5);
    policy.setSegmentsPerTier(8.0);

    // Verify configuration
    EXPECT_DOUBLE_EQ(1024.0, policy.getMaxMergedSegmentMB());
    EXPECT_DOUBLE_EQ(1.0, policy.getFloorSegmentMB());
    EXPECT_EQ(5, policy.getMaxMergeAtOnce());
    EXPECT_DOUBLE_EQ(8.0, policy.getSegmentsPerTier());
}
