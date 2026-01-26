// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace index {

/**
 * SegmentMerger merges multiple segments into a single segment
 *
 * Based on: org.apache.lucene.index.SegmentMerger
 *
 * Responsibilities:
 * - Merge terms and postings from source segments
 * - Merge doc values from source segments
 * - Merge stored fields from source segments
 * - Skip deleted documents (compact live docs)
 * - Write merged segment to disk
 * - Create new SegmentInfo for merged segment
 *
 * Thread safety: Not thread-safe. Use one SegmentMerger per merge.
 */
class SegmentMerger {
public:
    /**
     * Constructor
     * @param directory Directory for output segment
     * @param segmentName Name for merged segment
     * @param sourceSegments Segments to merge
     */
    SegmentMerger(store::Directory& directory, const std::string& segmentName,
                  const std::vector<std::shared_ptr<SegmentInfo>>& sourceSegments);

    /**
     * Perform the merge
     * @return SegmentInfo for merged segment
     */
    std::shared_ptr<SegmentInfo> merge();

private:
    // ==================== Helper Methods ====================

    /**
     * Build merged FieldInfos (union of all fields from source segments)
     */
    FieldInfos buildMergedFieldInfos();

    /**
     * Merge postings (terms and frequencies/positions)
     * Returns number of documents written
     */
    int mergePostings(const FieldInfos& mergedFieldInfos);

    /**
     * Merge doc values (numeric, binary, sorted)
     * @param docBase Starting doc ID for this segment
     */
    void mergeDocValues(const FieldInfos& mergedFieldInfos, int docBase);

    /**
     * Merge stored fields (original document content)
     * @param docBase Starting doc ID for this segment
     */
    void mergeStoredFields(int docBase);

    /**
     * Check if document is deleted in source segment
     */
    bool isDeleted(int sourceSegmentIdx, int docID);

    /**
     * Map old doc IDs to new doc IDs (accounting for deletions)
     * Returns new doc ID or -1 if deleted
     */
    int mapDocID(int sourceSegmentIdx, int oldDocID);

    // ==================== Members ====================

    store::Directory& directory_;
    std::string segmentName_;
    std::vector<std::shared_ptr<SegmentInfo>> sourceSegments_;

    // Doc ID mapping
    struct DocIDMapping {
        int newMaxDoc;                 // Number of docs after merge
        std::vector<int> segmentBases;  // Starting doc ID for each source segment
    };

    DocIDMapping docMapping_;
};

}  // namespace index
}  // namespace diagon
