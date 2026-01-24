// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace diagon {
namespace index {

// Forward declarations
class SegmentInfos;
class SegmentCommitInfo;
class MergeSpecification;
class OneMerge;

/**
 * What triggered the merge check
 */
enum class MergeTrigger {
    SEGMENT_FLUSH,      // After flushing new segment
    FULL_FLUSH,         // After full flush
    COMMIT,             // During commit
    GET_READER,         // When opening reader
    CLOSING,            // During close
    EXPLICIT            // Explicit forceMerge() call
};

/**
 * MergePolicy determines which segments to merge.
 *
 * Called by IndexWriter after flush/commit.
 * Returns MergeSpecification describing merges to perform.
 *
 * Based on: org.apache.lucene.index.MergePolicy
 *
 * NOTE: Stub implementation - provides interface only.
 * Concrete implementations (TieredMergePolicy, LogByteSizeMergePolicy) not yet implemented.
 */
class MergePolicy {
public:
    virtual ~MergePolicy() = default;

    // ==================== Merge Selection ====================

    /**
     * Find merges needed after flush
     * @param trigger What triggered this check
     * @param segmentInfos Current segments
     * @return MergeSpecification or nullptr if no merges needed
     */
    virtual MergeSpecification* findMerges(
        MergeTrigger trigger,
        const SegmentInfos& segmentInfos) = 0;

    /**
     * Find merge to run when segments are needed for searching
     * More aggressive than findMerges()
     */
    virtual MergeSpecification* findForcedMerges(
        const SegmentInfos& segmentInfos,
        int maxSegmentCount,
        const std::map<SegmentCommitInfo*, bool>& segmentsToMerge) = 0;

    /**
     * Find merges needed only to reclaim deletes
     */
    virtual MergeSpecification* findForcedDeletesMerges(
        const SegmentInfos& segmentInfos) = 0;

    // ==================== Configuration ====================

    /**
     * Set max merged segment size (MB)
     */
    virtual void setMaxMergedSegmentMB(double mb) = 0;

    /**
     * Set floor segment size (MB)
     * Segments below this are always eligible for merge
     */
    virtual void setFloorSegmentMB(double mb) = 0;

    /**
     * Get max merged segment size (MB)
     */
    virtual double getMaxMergedSegmentMB() const = 0;

    /**
     * Get floor segment size (MB)
     */
    virtual double getFloorSegmentMB() const = 0;

    // ==================== Utilities ====================

    /**
     * Check if segment is fully merged (no deletes)
     */
    virtual bool isMerged(const SegmentInfos& infos,
                         const SegmentCommitInfo& info) const {
        // Stub: would check info.getDelCount() == 0
        return true;
    }

    /**
     * Check if fully deleted segment should be kept
     */
    virtual bool keepFullyDeletedSegment(const SegmentCommitInfo& info) const {
        return false;  // Delete fully deleted segments by default
    }
};

}  // namespace index
}  // namespace diagon
