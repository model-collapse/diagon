// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace diagon {
namespace index {

// Forward declaration
class SegmentCommitInfo;

/**
 * OneMerge describes a single merge operation.
 *
 * Tracks segments to merge, merge progress, and result.
 *
 * Based on: org.apache.lucene.index.OneMerge
 *
 * NOTE: Stub implementation - provides interface only.
 * Full merge execution not yet implemented.
 */
class OneMerge {
public:
    /**
     * Merge state
     */
    enum class State {
        NOT_STARTED,
        RUNNING,
        PAUSED,
        ABORTED,
        COMPLETED
    };

    /**
     * Constructor
     * @param segments Segments to merge (pointers not owned)
     */
    explicit OneMerge(const std::vector<SegmentCommitInfo*>& segments)
        : segments_(segments)
        , totalDocCount_(0)
        , state_(State::NOT_STARTED) {
        // Would compute totalDocCount_ from segments
    }

    // ==================== Segment Info ====================

    /**
     * Segments being merged
     */
    const std::vector<SegmentCommitInfo*>& getSegments() const {
        return segments_;
    }

    /**
     * Total document count
     */
    size_t getTotalDocCount() const {
        return totalDocCount_;
    }

    // ==================== State ====================

    /**
     * Get merge state
     */
    State getState() const {
        return state_.load();
    }

    /**
     * Set merge state
     */
    void setState(State state) {
        state_.store(state);
    }

    /**
     * Is merge running?
     */
    bool isRunning() const {
        return state_.load() == State::RUNNING;
    }

    /**
     * Is merge aborted?
     */
    bool isAborted() const {
        return state_.load() == State::ABORTED;
    }

    // ==================== Description ====================

    /**
     * String representation
     */
    std::string segString() const {
        return "merge(" + std::to_string(segments_.size()) + " segments)";
    }

private:
    std::vector<SegmentCommitInfo*> segments_;
    size_t totalDocCount_;
    std::atomic<State> state_;
};

}  // namespace index
}  // namespace diagon
