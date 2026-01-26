// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/MergePolicy.h"
#include "diagon/index/SegmentInfo.h"

#include <memory>
#include <vector>

namespace diagon {
namespace index {

// Forward declarations
class MergeSpecification;
class OneMerge;

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

    // ==================== Merge Selection ====================

    MergeSpecification* findMerges(MergeTrigger trigger,
                                   const SegmentInfos& segmentInfos) override;

    MergeSpecification*
    findForcedMerges(const SegmentInfos& segmentInfos, int maxSegmentCount,
                     const std::map<SegmentCommitInfo*, bool>& segmentsToMerge) override;

    MergeSpecification* findForcedDeletesMerges(const SegmentInfos& segmentInfos) override;

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
    // ==================== Helper Structures ====================

    struct SegmentSize {
        std::shared_ptr<SegmentInfo> info;
        int64_t sizeInBytes;
        int maxDoc;
        int delCount;

        SegmentSize(std::shared_ptr<SegmentInfo> info_)
            : info(info_)
            , sizeInBytes(info_->sizeInBytes())
            , maxDoc(info_->maxDoc())
            , delCount(info_->delCount()) {}

        // Size adjusted for deletions (bytes * (1 - delPct))
        int64_t adjustedSize() const {
            if (maxDoc == 0)
                return 0;
            double pct = 1.0 - (double)delCount / maxDoc;
            return (int64_t)(sizeInBytes * pct);
        }
    };

    // ==================== Helper Methods ====================

    /**
     * Sort segments by size (largest first)
     */
    std::vector<SegmentSize> getSortedSegments(const SegmentInfos& infos) const;

    /**
     * Calculate allowed segment count based on total index size
     */
    int calculateAllowedSegmentCount(int64_t totalBytes, int64_t minSegmentBytes) const;

    /**
     * Find best merge from eligible segments
     */
    OneMerge* findBestMerge(const std::vector<SegmentSize>& eligible, int64_t maxBytes) const;

    /**
     * Compute merge skew (largest / smallest)
     */
    double computeSkew(const std::vector<SegmentSize*>& segments) const;

    double maxMergedSegmentMB_;
    double floorSegmentMB_;
    int maxMergeAtOnce_;
    double segmentsPerTier_;
};

}  // namespace index
}  // namespace diagon
