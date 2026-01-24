// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DirectoryReader.h"

#include <stdexcept>

namespace diagon {
namespace index {

// ==================== Factory Methods ====================

std::shared_ptr<DirectoryReader> DirectoryReader::open(store::Directory& dir) {
    // Read latest commit (segments_N file)
    SegmentInfos segmentInfos = SegmentInfos::readLatestCommit(dir);

    // Create SegmentReader for each segment
    auto segmentReaders = createSegmentReaders(dir, segmentInfos);

    // Construct DirectoryReader
    return std::shared_ptr<DirectoryReader>(
        new DirectoryReader(dir, std::move(segmentReaders), segmentInfos));
}

// ==================== Constructor/Destructor ====================

DirectoryReader::DirectoryReader(store::Directory& dir,
                                 std::vector<std::shared_ptr<SegmentReader>> readers,
                                 const SegmentInfos& segmentInfos)
    : directory_(dir)
    , segmentReaders_(std::move(readers))
    , segmentInfos_(segmentInfos) {}

DirectoryReader::~DirectoryReader() {
    // Destructor - resources cleaned up in doClose()
}

// ==================== CompositeReader Implementation ====================

std::vector<std::shared_ptr<IndexReader>> DirectoryReader::getSequentialSubReaders() const {
    ensureOpen();

    // Convert SegmentReader vector to IndexReader vector
    std::vector<std::shared_ptr<IndexReader>> result;
    result.reserve(segmentReaders_.size());
    for (const auto& reader : segmentReaders_) {
        result.push_back(reader);
    }
    return result;
}

// ==================== Lifecycle ====================

void DirectoryReader::doClose() {
    // Close all segment readers (decrement their refCount)
    // This marks them as closed, but leaves the objects alive
    // until the DirectoryReader is destroyed
    for (auto& reader : segmentReaders_) {
        if (reader && reader->getRefCount() > 0) {
            reader->decRef();
        }
    }

    // Note: We do NOT clear segmentReaders_ here because raw pointers
    // from leaves() might still be in use. The vector will be cleared
    // when the DirectoryReader is destroyed.

    // Call parent doClose to set closed flag
    IndexReader::doClose();
}

// ==================== Helper Methods ====================

std::vector<std::shared_ptr<SegmentReader>>
DirectoryReader::createSegmentReaders(store::Directory& dir, const SegmentInfos& sis) {
    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(sis.size());

    // Create a SegmentReader for each segment
    for (int i = 0; i < sis.size(); i++) {
        auto segmentInfo = sis.info(i);
        auto reader = SegmentReader::open(dir, segmentInfo);
        readers.push_back(reader);
    }

    return readers;
}

}  // namespace index
}  // namespace diagon
