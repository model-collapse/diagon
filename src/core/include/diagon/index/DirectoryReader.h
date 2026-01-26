// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/IndexReader.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/store/Directory.h"

#include <memory>
#include <vector>

namespace diagon {
namespace index {

/**
 * DirectoryReader - CompositeReader for reading an index directory
 *
 * Phase 4 implementation:
 * - Opens segments from segments_N file
 * - Creates SegmentReader for each segment
 * - Provides composite view of all segments
 * - No NRT support (no IndexWriter integration)
 * - No reader reuse/reopen
 *
 * Thread-safe for concurrent reads after construction.
 *
 * Usage:
 *   auto reader = DirectoryReader::open(*directory);
 *   for (const auto& leaf : reader->leaves()) {
 *       // Access each segment via leaf.reader()
 *   }
 *   reader->close();
 *
 * Based on:
 * - org.apache.lucene.index.DirectoryReader
 * - org.apache.lucene.index.StandardDirectoryReader
 */
class DirectoryReader : public CompositeReader {
public:
    /**
     * Open DirectoryReader for the latest commit in the directory
     *
     * @param dir Directory containing the index
     * @return DirectoryReader instance
     * @throws IOException if no index found or error reading
     */
    static std::shared_ptr<DirectoryReader> open(store::Directory& dir);

    /**
     * If the index has changed since oldReader was opened, open and return a new reader;
     * else, return nullptr.
     *
     * This method is typically far less costly than opening a fully new DirectoryReader
     * as it shares resources (segment readers) with the provided oldReader when possible.
     *
     * The provided reader is NOT closed (you are responsible for doing so).
     *
     * @param oldReader Previous reader to check against
     * @return nullptr if no changes; else, a new DirectoryReader instance
     * @throws IOException if there is an IO error
     */
    static std::shared_ptr<DirectoryReader> openIfChanged(std::shared_ptr<DirectoryReader> oldReader);

    /**
     * Destructor
     */
    ~DirectoryReader() override;

    // ==================== CompositeReader Implementation ====================

    /**
     * Get sequential sub-readers (SegmentReaders)
     */
    std::vector<std::shared_ptr<IndexReader>> getSequentialSubReaders() const override;

    /**
     * Get reader cache helper
     *
     * For DirectoryReader, this is invalidated whenever the index changes:
     * - New segments added
     * - Segments merged
     * - Deletions applied
     *
     * Safe to cache:
     * - Total document counts
     * - Index statistics
     * - Reader-level aggregations
     */
    CacheHelper* getReaderCacheHelper() const override {
        return const_cast<CacheHelper*>(&readerCacheHelper_);
    }

    // ==================== DirectoryReader-specific methods ====================

    /**
     * Get the directory this reader is reading from
     */
    store::Directory& directory() const { return directory_; }

    /**
     * Get the SegmentInfos this reader is reading
     */
    const SegmentInfos& getSegmentInfos() const {
        ensureOpen();
        return segmentInfos_;
    }

    /**
     * Get the index version (generation number)
     */
    int64_t getVersion() const {
        ensureOpen();
        return segmentInfos_.getVersion();
    }

protected:
    /**
     * Called when closing (refCount reaches 0)
     */
    void doClose() override;

    /**
     * Subclass implementation of openIfChanged
     * @return new reader if changed, nullptr if not changed
     */
    virtual std::shared_ptr<DirectoryReader> doOpenIfChanged();

private:
    /**
     * Private constructor - use open() factory method
     */
    DirectoryReader(store::Directory& dir, std::vector<std::shared_ptr<SegmentReader>> readers,
                    const SegmentInfos& segmentInfos);

    /**
     * Create SegmentReaders for all segments in SegmentInfos
     */
    static std::vector<std::shared_ptr<SegmentReader>>
    createSegmentReaders(store::Directory& dir, const SegmentInfos& sis);

    /**
     * Create SegmentReaders for new SegmentInfos, reusing readers where possible
     * @param dir Directory
     * @param newInfos New segment infos
     * @param oldReaders Old segment readers to potentially reuse
     * @param oldInfos Old segment infos (for matching)
     * @return New segment readers (mix of reused and newly opened)
     */
    static std::vector<std::shared_ptr<SegmentReader>>
    createSegmentReadersWithReuse(store::Directory& dir, const SegmentInfos& newInfos,
                                    const std::vector<std::shared_ptr<SegmentReader>>& oldReaders,
                                    const SegmentInfos& oldInfos);

    /**
     * Find matching segment in old readers list
     * @return index of matching segment, or -1 if not found
     */
    static int findSegment(const std::shared_ptr<SegmentInfo>& target,
                           const SegmentInfos& oldInfos);

    // Directory containing the index
    store::Directory& directory_;

    // Segment readers (one per segment)
    std::vector<std::shared_ptr<SegmentReader>> segmentReaders_;

    // Segment metadata from segments_N file
    SegmentInfos segmentInfos_;

    // Cache helper (Phase 5)
    // Invalidated when index changes (new segments, merges, deletions)
    CacheHelper readerCacheHelper_;
};

}  // namespace index
}  // namespace diagon
