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
     */
    CacheHelper* getReaderCacheHelper() const override {
        return nullptr;  // Phase 4: No caching support
    }

    // ==================== DirectoryReader-specific methods ====================

    /**
     * Get the directory this reader is reading from
     */
    store::Directory& directory() const {
        return directory_;
    }

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

private:
    /**
     * Private constructor - use open() factory method
     */
    DirectoryReader(
        store::Directory& dir,
        std::vector<std::shared_ptr<SegmentReader>> readers,
        const SegmentInfos& segmentInfos
    );

    /**
     * Create SegmentReaders for all segments in SegmentInfos
     */
    static std::vector<std::shared_ptr<SegmentReader>> createSegmentReaders(
        store::Directory& dir,
        const SegmentInfos& sis
    );

    // Directory containing the index
    store::Directory& directory_;

    // Segment readers (one per segment)
    std::vector<std::shared_ptr<SegmentReader>> segmentReaders_;

    // Segment metadata from segments_N file
    SegmentInfos segmentInfos_;
};

}  // namespace index
}  // namespace diagon
