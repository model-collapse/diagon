// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <limits>

namespace diagon {
namespace search {

// Forward declarations
class LeafCollector;

/**
 * BulkScorer drives iteration and collection for a segment.
 *
 * Unlike Scorer (which exposes nextDoc()/advance() and relies on
 * IndexSearcher for the iteration loop), BulkScorer owns the
 * iteration internally and calls collector.collect() in batches.
 *
 * This enables window-based optimizations:
 * - Priority queue ops only at window boundaries (not per-document)
 * - Essential/non-essential clause partitioning per window
 * - Bitset + score array batch collection within 4096-doc windows
 *
 * Based on: org.apache.lucene.search.BulkScorer
 */
class BulkScorer {
public:
    virtual ~BulkScorer() = default;

    /**
     * Score all matching documents in [min, max) and pass them to collector.
     *
     * @param collector LeafCollector to receive hits
     * @param min Minimum doc ID (inclusive)
     * @param max Maximum doc ID (exclusive), or NO_MORE_DOCS for all
     * @return An approximation of the next matching doc after max,
     *         or max if unknown, or NO_MORE_DOCS if done.
     */
    virtual int score(LeafCollector* collector, int min, int max) = 0;

    /**
     * Estimated cost of iterating over all matching documents.
     */
    virtual int64_t cost() const = 0;

    static constexpr int NO_MORE_DOCS = std::numeric_limits<int>::max();
};

}  // namespace search
}  // namespace diagon
