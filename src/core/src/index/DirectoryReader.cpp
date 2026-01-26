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

// ==================== Reader Reopening (NRT) ====================

std::shared_ptr<DirectoryReader> DirectoryReader::openIfChanged(
    std::shared_ptr<DirectoryReader> oldReader) {
    if (!oldReader) {
        return nullptr;
    }

    oldReader->ensureOpen();
    return oldReader->doOpenIfChanged();
}

std::shared_ptr<DirectoryReader> DirectoryReader::doOpenIfChanged() {
    ensureOpen();

    // Read latest commit from directory
    int64_t latestGeneration = SegmentInfos::findMaxGeneration(directory_);

    if (latestGeneration < 0) {
        // No index exists
        return nullptr;
    }

    // Check if generation has changed
    if (latestGeneration == segmentInfos_.getGeneration()) {
        // No changes
        return nullptr;
    }

    // Read new SegmentInfos
    std::string fileName = SegmentInfos::getSegmentsFileName(latestGeneration);
    SegmentInfos newInfos = SegmentInfos::read(directory_, fileName);

    // Create segment readers, reusing old ones where possible
    auto newReaders =
        createSegmentReadersWithReuse(directory_, newInfos, segmentReaders_, segmentInfos_);

    // Create new DirectoryReader
    return std::shared_ptr<DirectoryReader>(
        new DirectoryReader(directory_, std::move(newReaders), newInfos));
}

std::vector<std::shared_ptr<SegmentReader>>
DirectoryReader::createSegmentReadersWithReuse(
    store::Directory& dir, const SegmentInfos& newInfos,
    const std::vector<std::shared_ptr<SegmentReader>>& oldReaders, const SegmentInfos& oldInfos) {

    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(newInfos.size());

    for (int i = 0; i < newInfos.size(); i++) {
        auto newSegInfo = newInfos.info(i);

        // Try to find matching segment in old readers
        int oldIdx = findSegment(newSegInfo, oldInfos);

        if (oldIdx >= 0 && oldIdx < static_cast<int>(oldReaders.size())) {
            // Check if segment is identical (name matches and no new deletions)
            auto oldSegInfo = oldInfos.info(oldIdx);

            if (newSegInfo->name() == oldSegInfo->name() &&
                newSegInfo->delCount() == oldSegInfo->delCount() &&
                newSegInfo->maxDoc() == oldSegInfo->maxDoc()) {
                // Segment unchanged - reuse reader
                auto reusedReader = oldReaders[oldIdx];
                reusedReader->incRef();  // Increment ref count for new reader
                readers.push_back(reusedReader);
                continue;
            }
        }

        // Segment changed or new - open new reader
        auto newReader = SegmentReader::open(dir, newSegInfo);
        readers.push_back(newReader);
    }

    return readers;
}

int DirectoryReader::findSegment(const std::shared_ptr<SegmentInfo>& target,
                                  const SegmentInfos& oldInfos) {
    // Search for segment by name
    for (int i = 0; i < oldInfos.size(); i++) {
        if (oldInfos.info(i)->name() == target->name()) {
            return i;
        }
    }
    return -1;  // Not found
}

}  // namespace index
}  // namespace diagon
