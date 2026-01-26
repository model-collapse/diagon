// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>

namespace diagon {

// Forward declarations
namespace store {
class Directory;
class IOContext;
}  // namespace store

namespace index {
class FieldInfos;
class SegmentInfo;
}  // namespace index

}  // namespace diagon

namespace diagon {
namespace codecs {

// Forward declarations (to be implemented in future tasks)
class BufferedUpdates;

/**
 * Shared state for writing a segment
 *
 * Passed to format consumers during segment flush
 */
struct SegmentWriteState {
    store::Directory& directory;
    std::string segmentName;
    std::string segmentSuffix;  // For multi-format support
    store::IOContext& context;
    index::SegmentInfo* segmentInfo{nullptr};  // Optional

    // TODO: Add when implemented:
    // FieldInfos& fieldInfos;
    // BufferedUpdates* deletes;    // May be nullptr

    SegmentWriteState(store::Directory& dir, const std::string& name, const std::string& suffix,
                      store::IOContext& ctx)
        : directory(dir)
        , segmentName(name)
        , segmentSuffix(suffix)
        , context(ctx) {}
};

/**
 * Shared state for reading a segment
 *
 * Passed to format producers when opening segment
 */
struct SegmentReadState {
    store::Directory& directory;
    std::string segmentName;
    std::string segmentSuffix;
    store::IOContext& context;
    index::SegmentInfo* segmentInfo{nullptr};  // Optional

    // TODO: Add when implemented:
    // FieldInfos& fieldInfos;

    SegmentReadState(store::Directory& dir, const std::string& name, const std::string& suffix,
                     store::IOContext& ctx)
        : directory(dir)
        , segmentName(name)
        , segmentSuffix(suffix)
        , context(ctx) {}
};

}  // namespace codecs
}  // namespace diagon
