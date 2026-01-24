// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>

namespace diagon {
namespace index {

// Forward declarations
class OneMerge;
class IndexWriter;

/**
 * MergeScheduler executes merges.
 *
 * Runs merges in background threads or inline.
 *
 * Based on: org.apache.lucene.index.MergeScheduler
 *
 * NOTE: Stub implementation - provides interface only.
 * Concrete implementations (ConcurrentMergeScheduler, SerialMergeScheduler) not yet implemented.
 */
class MergeScheduler {
public:
    virtual ~MergeScheduler() = default;

    /**
     * Schedule a merge for execution
     * @param writer IndexWriter performing the merge
     * @param merge Merge to execute
     */
    virtual void merge(IndexWriter& writer, OneMerge& merge) = 0;

    /**
     * Close scheduler and wait for pending merges
     */
    virtual void close() = 0;

    /**
     * Get number of pending merges
     */
    virtual size_t getPendingMergeCount() const = 0;
};

}  // namespace index
}  // namespace diagon
