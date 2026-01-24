// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IOContext.h"

#include <memory>
#include <string>

namespace diagon {
namespace index {

// Forward declarations
class SegmentInfo;

/**
 * Holder class for common parameters used during segment write.
 *
 * Based on: org.apache.lucene.index.SegmentWriteState
 */
struct SegmentWriteState {
    /**
     * Directory where this segment will be written to.
     */
    store::Directory* directory;

    /**
     * SegmentInfo describing this segment.
     * NOTE: For Phase 2, we'll use a simplified SegmentInfo struct.
     */
    std::string segmentName;

    /**
     * Number of documents in this segment.
     */
    int maxDoc;

    /**
     * FieldInfos describing all fields in this segment.
     */
    FieldInfos fieldInfos;

    /**
     * Unique suffix for any postings files written for this segment.
     * Empty string if no suffix.
     */
    std::string segmentSuffix;

    /**
     * IOContext for all writes.
     */
    store::IOContext context;

    /**
     * Constructor
     */
    SegmentWriteState(store::Directory* dir, const std::string& segName, int maxDocs,
                      const FieldInfos& fis, const std::string& suffix = "")
        : directory(dir)
        , segmentName(segName)
        , maxDoc(maxDocs)
        , fieldInfos(fis)
        , segmentSuffix(suffix)
        , context(store::IOContext::DEFAULT) {}
};

/**
 * Holder class for common parameters used during segment read.
 *
 * Based on: org.apache.lucene.index.SegmentReadState
 */
struct SegmentReadState {
    /**
     * Directory where this segment is stored.
     */
    store::Directory* directory;

    /**
     * SegmentInfo describing this segment.
     */
    std::string segmentName;

    /**
     * Number of documents in this segment.
     */
    int maxDoc;

    /**
     * FieldInfos describing all fields in this segment.
     */
    FieldInfos fieldInfos;

    /**
     * Unique suffix for any postings files written for this segment.
     */
    std::string segmentSuffix;

    /**
     * IOContext for all reads.
     */
    store::IOContext context;

    /**
     * Constructor
     */
    SegmentReadState(store::Directory* dir, const std::string& segName, int maxDocs,
                     const FieldInfos& fis, const std::string& suffix = "")
        : directory(dir)
        , segmentName(segName)
        , maxDoc(maxDocs)
        , fieldInfos(fis)
        , segmentSuffix(suffix)
        , context(store::IOContext::DEFAULT) {}
};

}  // namespace index
}  // namespace diagon
