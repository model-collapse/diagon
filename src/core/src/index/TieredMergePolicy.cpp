// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/TieredMergePolicy.h"

#include "diagon/index/MergeSpecification.h"
#include "diagon/index/OneMerge.h"
#include "diagon/index/SegmentInfo.h"

#include <algorithm>
#include <cmath>

namespace diagon {
namespace index {

// ==================== Helper Methods ====================

std::vector<TieredMergePolicy::SegmentSize>
TieredMergePolicy::getSortedSegments(const SegmentInfos& infos) const {
    std::vector<SegmentSize> segments;
    segments.reserve(infos.size());

    for (int i = 0; i < infos.size(); i++) {
        segments.emplace_back(infos.info(i));
    }

    // Sort by adjusted size (descending)
    std::sort(segments.begin(), segments.end(), [](const SegmentSize& a, const SegmentSize& b) {
        return a.adjustedSize() > b.adjustedSize();
    });

    return segments;
}

int TieredMergePolicy::calculateAllowedSegmentCount(int64_t totalBytes,
                                                    int64_t minSegmentBytes) const {
    // Calculate how many segments are allowed based on tier size
    // Start with floor segment size as level 0
    int64_t levelSize = std::max(minSegmentBytes,
                                 static_cast<int64_t>(floorSegmentMB_ * 1024 * 1024));
    int64_t bytesLeft = totalBytes;
    double allowedSegCount = 0;

    int mergeFactor = static_cast<int>(segmentsPerTier_);

    // Compute tier sizes: level 0, level 0 * factor, level 0 * factor^2, etc.
    while (true) {
        double segCountLevel = bytesLeft / (double)levelSize;
        if (segCountLevel < segmentsPerTier_ ||
            levelSize >= static_cast<int64_t>(maxMergedSegmentMB_ * 1024 * 1024)) {
            allowedSegCount += std::ceil(segCountLevel);
            break;
        }
        allowedSegCount += segmentsPerTier_;
        bytesLeft -= static_cast<int64_t>(segmentsPerTier_ * levelSize);
        levelSize = std::min(static_cast<int64_t>(maxMergedSegmentMB_ * 1024 * 1024),
                             levelSize * mergeFactor);
    }

    // Always allow at least segsPerTier segments
    return std::max(static_cast<int>(allowedSegCount), mergeFactor);
}

double TieredMergePolicy::computeSkew(const std::vector<SegmentSize*>& segments) const {
    if (segments.empty())
        return 1.0;

    int64_t minSize = segments[0]->adjustedSize();
    int64_t maxSize = segments[0]->adjustedSize();

    for (const auto* seg : segments) {
        int64_t size = seg->adjustedSize();
        minSize = std::min(minSize, size);
        maxSize = std::max(maxSize, size);
    }

    if (minSize == 0)
        return 1e9;  // Very high skew if one segment is empty

    return (double)maxSize / minSize;
}

OneMerge* TieredMergePolicy::findBestMerge(const std::vector<SegmentSize>& eligible,
                                           int64_t maxBytes) const {
    if (eligible.size() < 2) {
        return nullptr;  // Need at least 2 segments to merge
    }

    // Try to find best merge with low skew and reasonable size
    OneMerge* bestMerge = nullptr;
    double bestScore = 1e9;  // Lower is better

    int maxMerge = std::min(static_cast<int>(eligible.size()), maxMergeAtOnce_);

    // Try different merge sizes (2, 3, 4, ... up to maxMergeAtOnce)
    for (int mergeSize = 2; mergeSize <= maxMerge; mergeSize++) {
        // Try consecutive segments (they should be similar size after sorting)
        for (size_t start = 0; start + mergeSize <= eligible.size(); start++) {
            // Collect segments for this merge
            std::vector<SegmentSize*> mergeSegments;
            int64_t totalSize = 0;

            for (size_t i = start; i < start + mergeSize; i++) {
                mergeSegments.push_back(
                    const_cast<SegmentSize*>(&eligible[i]));  // Safe: we're not modifying
                totalSize += eligible[i].adjustedSize();
            }

            // Skip if merged segment would be too large
            if (totalSize > maxBytes) {
                continue;
            }

            // Compute score: skew + normalized size penalty
            double skew = computeSkew(mergeSegments);
            double sizePenalty = (double)totalSize / maxBytes;  // 0..1
            double score = skew + sizePenalty;

            if (score < bestScore) {
                bestScore = score;

                // Create OneMerge
                std::vector<SegmentCommitInfo*> segmentPtrs;  // Empty for now (we use SegmentInfo)
                if (bestMerge) {
                    delete bestMerge;
                }

                // For now, we'll store SegmentInfo* cast to SegmentCommitInfo*
                // This is a temporary simplification until we implement SegmentCommitInfo
                for (auto* seg : mergeSegments) {
                    segmentPtrs.push_back(reinterpret_cast<SegmentCommitInfo*>(seg->info.get()));
                }
                bestMerge = new OneMerge(segmentPtrs);
            }
        }
    }

    return bestMerge;
}

// ==================== Merge Selection ====================

MergeSpecification* TieredMergePolicy::findMerges(MergeTrigger trigger,
                                                  const SegmentInfos& segmentInfos) {
    if (segmentInfos.size() < 2) {
        return nullptr;  // Need at least 2 segments
    }

    // Get sorted segments
    auto sortedSegments = getSortedSegments(segmentInfos);

    // Calculate total index size and min segment size
    int64_t totalBytes = 0;
    int64_t minSegmentBytes = std::numeric_limits<int64_t>::max();

    for (const auto& seg : sortedSegments) {
        totalBytes += seg.adjustedSize();
        minSegmentBytes = std::min(minSegmentBytes, seg.adjustedSize());
    }

    // Calculate how many segments we're allowed
    int allowedSegCount = calculateAllowedSegmentCount(totalBytes, minSegmentBytes);

    // If we're under budget, no merge needed
    if (static_cast<int>(sortedSegments.size()) <= allowedSegCount) {
        return nullptr;
    }

    // Find eligible segments (exclude very large ones)
    std::vector<SegmentSize> eligible;
    int64_t maxBytes = static_cast<int64_t>(maxMergedSegmentMB_ * 1024 * 1024);

    for (const auto& seg : sortedSegments) {
        // Skip segments that are already too large
        if (seg.adjustedSize() < maxBytes / 2) {
            eligible.push_back(seg);
        }
    }

    if (eligible.size() < 2) {
        return nullptr;  // Not enough eligible segments
    }

    // Find best merge
    OneMerge* merge = findBestMerge(eligible, maxBytes);
    if (!merge) {
        return nullptr;
    }

    // Create specification
    auto* spec = new MergeSpecification();
    spec->add(std::unique_ptr<OneMerge>(merge));
    return spec;
}

MergeSpecification*
TieredMergePolicy::findForcedMerges(const SegmentInfos& segmentInfos, int maxSegmentCount,
                                    const std::map<SegmentCommitInfo*, bool>& segmentsToMerge) {
    if (segmentInfos.size() <= maxSegmentCount) {
        return nullptr;  // Already at or below target
    }

    // Get sorted segments
    auto sortedSegments = getSortedSegments(segmentInfos);

    // For forced merge, we'll merge all segments down to maxSegmentCount
    // Simple strategy: merge smallest segments first
    auto* spec = new MergeSpecification();

    // Reverse sort to merge smallest first
    std::reverse(sortedSegments.begin(), sortedSegments.end());

    // Group segments into maxSegmentCount groups
    int segmentsPerGroup = (sortedSegments.size() + maxSegmentCount - 1) /
                           maxSegmentCount;  // Round up

    for (int group = 0; group < maxSegmentCount && !sortedSegments.empty(); group++) {
        std::vector<SegmentCommitInfo*> toMerge;
        int take = std::min(segmentsPerGroup, static_cast<int>(sortedSegments.size()));

        for (int i = 0; i < take; i++) {
            toMerge.push_back(reinterpret_cast<SegmentCommitInfo*>(sortedSegments[i].info.get()));
        }

        sortedSegments.erase(sortedSegments.begin(), sortedSegments.begin() + take);

        if (toMerge.size() > 1) {  // Only create merge if multiple segments
            spec->add(std::make_unique<OneMerge>(toMerge));
        }
    }

    if (spec->empty()) {
        delete spec;
        return nullptr;
    }

    return spec;
}

MergeSpecification* TieredMergePolicy::findForcedDeletesMerges(const SegmentInfos& segmentInfos) {
    // Find segments with high delete ratios and merge them
    std::vector<SegmentSize> toMerge;

    for (int i = 0; i < segmentInfos.size(); i++) {
        auto info = segmentInfos.info(i);
        if (info->maxDoc() == 0)
            continue;

        double deletePct = 100.0 * info->delCount() / info->maxDoc();

        // Merge if >20% deleted
        if (deletePct > 20.0) {
            toMerge.emplace_back(info);
        }
    }

    if (toMerge.size() < 2) {
        return nullptr;  // Need at least 2 segments
    }

    // Create single merge of all high-delete segments
    std::vector<SegmentCommitInfo*> segmentPtrs;
    for (auto& seg : toMerge) {
        segmentPtrs.push_back(reinterpret_cast<SegmentCommitInfo*>(seg.info.get()));
    }

    auto* spec = new MergeSpecification();
    spec->add(std::make_unique<OneMerge>(segmentPtrs));
    return spec;
}

}  // namespace index
}  // namespace diagon
