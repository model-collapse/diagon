// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/DocIdSetIterator.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace search {

// Forward declarations
class Weight;

/**
 * Scorer iterates over matching documents with scores.
 *
 * Extends DocIdSetIterator with scoring capability.
 * One Scorer per segment.
 *
 * Based on: org.apache.lucene.search.Scorer
 *
 * NOTE: Stub implementation - provides interface only.
 * Full implementation requires:
 * - Scoring models (BM25, TF-IDF)
 * - Postings format integration
 * - Two-phase iteration support
 * - WAND optimization
 */
class Scorer : public DocIdSetIterator {
public:
    virtual ~Scorer() = default;

    /**
     * Current document score
     * Only valid after nextDoc() or advance()
     */
    virtual float score() const = 0;

    /**
     * Get smoothing score
     * Used for global statistics in distributed search
     */
    virtual float smoothingScore(int docId) const { return 0.0f; }

    /**
     * Get parent weight
     */
    virtual const Weight& getWeight() const = 0;

    // ==================== Score Upper Bounds ====================

    /**
     * Maximum possible score for docs in [upTo, ∞)
     * Used for early termination (WAND)
     */
    virtual float getMaxScore(int upTo) const { return std::numeric_limits<float>::max(); }

    /**
     * Shallow advance to doc >= target
     * Cheaper than advance(), doesn't position for scoring
     */
    virtual int advanceShallow(int target) { return advance(target); }

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

    /**
     * Get total number of documents that matched the query.
     *
     * This includes ALL matching documents, not just those collected for top-K.
     * For scorers with early termination (e.g., WAND), this count may be higher
     * than the number of documents passed to the collector.
     *
     * @return Total matching document count, or -1 if not tracked
     */
    virtual int getTotalMatches() const {
        return -1;  // Default: not tracked
    }

    /**
     * Get next block boundary after target for smart max score updates.
     *
     * Used by WANDScorer (Phase 2) to align max score updates with actual
     * block boundaries instead of using fixed 128-doc windows.
     *
     * @param target Target doc ID to search from
     * @return Next block boundary doc ID, or NO_MORE_DOCS if no more blocks
     */
    virtual int getNextBlockBoundary(int target) const {
        // Default: Fixed 128-doc window (backward compatible)
        return (target < NO_MORE_DOCS - 128) ? target + 128 : NO_MORE_DOCS;
    }

    // ==================== Batch Scoring ====================

    static constexpr int SCORER_BATCH_SIZE = 32;

    /**
     * Score a batch of documents starting from the current position.
     *
     * Outputs docs and scores for all docs in [docID(), upTo), up to maxCount.
     * After return, docID() is the first doc >= upTo, or NO_MORE_DOCS.
     *
     * @param upTo Upper bound (exclusive) for doc IDs to process
     * @param outDocs Output array for doc IDs (caller-allocated, size >= maxCount)
     * @param outScores Output array for scores (caller-allocated, size >= maxCount)
     * @param maxCount Maximum docs to output per call
     * @return Number of docs output (0 if no docs < upTo remain)
     */
    virtual int scoreBatch(int upTo, int* outDocs, float* outScores, int maxCount) {
        int count = 0;
        int doc = docID();
        while (doc < upTo && doc != NO_MORE_DOCS && count < maxCount) {
            outDocs[count] = doc;
            outScores[count] = score();
            count++;
            doc = nextDoc();
        }
        return count;
    }
};

/**
 * Child scorer in complex queries
 */
struct ChildScorable {
    Scorer* child;
    std::string relationship;  // "MUST", "SHOULD", "MUST_NOT"
};

}  // namespace search
}  // namespace diagon
