// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/MergePolicy.h"

namespace diagon {
namespace index {

/**
 * TieredMergePolicy merges segments into tiers by size.
 *
 * Default policy - good balance between write amplification and search performance.
 *
 * Based on: org.apache.lucene.index.TieredMergePolicy
 *
 * NOTE: Stub implementation - provides interface only.
 */
class TieredMergePolicy : public MergePolicy {
public:
    TieredMergePolicy()
        : maxMergedSegmentMB_(5 * 1024.0)  // 5GB default
        , floorSegmentMB_(2.0)             // 2MB default
        , maxMergeAtOnce_(10)
        , segmentsPerTier_(10.0) {}

    // ==================== Merge Selection (Stubs) ====================

    MergeSpecification* findMerges(MergeTrigger trigger,
                                   const SegmentInfos& segmentInfos) override {
        // Stub: would analyze segment sizes and select merges
        return nullptr;
    }

    MergeSpecification*
    findForcedMerges(const SegmentInfos& segmentInfos, int maxSegmentCount,
                     const std::map<SegmentCommitInfo*, bool>& segmentsToMerge) override {
        // Stub: would merge down to maxSegmentCount segments
        return nullptr;
    }

    MergeSpecification* findForcedDeletesMerges(const SegmentInfos& segmentInfos) override {
        // Stub: would merge segments with high delete ratios
        return nullptr;
    }

    // ==================== Configuration ====================

    void setMaxMergedSegmentMB(double mb) override { maxMergedSegmentMB_ = mb; }

    void setFloorSegmentMB(double mb) override { floorSegmentMB_ = mb; }

    double getMaxMergedSegmentMB() const override { return maxMergedSegmentMB_; }

    double getFloorSegmentMB() const override { return floorSegmentMB_; }

    void setMaxMergeAtOnce(int max) { maxMergeAtOnce_ = max; }

    int getMaxMergeAtOnce() const { return maxMergeAtOnce_; }

    void setSegmentsPerTier(double segs) { segmentsPerTier_ = segs; }

    double getSegmentsPerTier() const { return segmentsPerTier_; }

private:
    double maxMergedSegmentMB_;
    double floorSegmentMB_;
    int maxMergeAtOnce_;
    double segmentsPerTier_;
};

}  // namespace index
}  // namespace diagon
