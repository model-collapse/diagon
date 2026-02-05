// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/ScoreMode.h"

namespace diagon {
namespace search {

// Forward declarations
class LeafCollector;
class Scorable;
}  // namespace search

namespace index {
struct LeafReaderContext;
}

namespace search {

/**
 * Expert: Collector decouples the score from the collected doc.
 *
 * Collectors are primarily meant to be used to collect raw results from a
 * search, and implement sorting or custom filtering.
 *
 * Based on: org.apache.lucene.search.Collector
 *
 * Usage:
 * ```cpp
 * Collector* collector = ...;
 * IndexSearcher searcher = ...;
 * searcher.search(query, collector);
 * ```
 */
class Collector {
public:
    virtual ~Collector() = default;

    /**
     * Create a LeafCollector for collecting hits in a single leaf segment.
     *
     * @param context Leaf context for this segment
     * @return LeafCollector for this segment
     */
    virtual LeafCollector* getLeafCollector(const index::LeafReaderContext& context) = 0;

    /**
     * Indicates what features are required from the scorer.
     *
     * @return ScoreMode indicating scoring requirements
     */
    virtual ScoreMode scoreMode() const = 0;
};

/**
 * Scorable provides access to the score of the current document.
 *
 * Based on: org.apache.lucene.search.Scorable
 */
class Scorable {
public:
    virtual ~Scorable() = default;

    /**
     * Returns the score of the current document.
     *
     * @return Current document's score
     */
    virtual float score() = 0;

    /**
     * Returns the doc ID of the current document.
     *
     * @return Current document ID
     */
    virtual int docID() = 0;

    /**
     * Set minimum competitive score for early termination (P0 Task #39)
     *
     * Called by collector when the threshold changes (e.g., heap fills up).
     * Scorers like WANDScorer use this to skip documents that cannot possibly
     * beat this score.
     *
     * @param minScore New minimum competitive score
     */
    virtual void setMinCompetitiveScore(float minScore) {
        // Default: no-op (not all scorers support this)
    }
};

/**
 * Collects hits for a single leaf segment.
 *
 * Based on: org.apache.lucene.search.LeafCollector
 */
class LeafCollector {
public:
    virtual ~LeafCollector() = default;

    /**
     * Called before collecting from a segment.
     * Sets the scorer that will be used for collecting.
     *
     * @param scorer Scorer to use for this segment
     */
    virtual void setScorer(Scorable* scorer) = 0;

    /**
     * Called once for every document matching a query.
     *
     * @param doc Document ID (relative to current segment)
     */
    virtual void collect(int doc) = 0;

    /**
     * Called after finishing collecting from a segment.
     * Allows collectors to flush any batched/buffered data.
     *
     * Default implementation does nothing.
     */
    virtual void finishSegment() {}
};

}  // namespace search
}  // namespace diagon
