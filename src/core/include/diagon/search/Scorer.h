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
    virtual float smoothingScore(int docId) const {
        return 0.0f;
    }

    /**
     * Get parent weight
     */
    virtual const Weight& getWeight() const = 0;

    // ==================== Score Upper Bounds ====================

    /**
     * Maximum possible score for docs in [upTo, ∞)
     * Used for early termination (WAND)
     */
    virtual float getMaxScore(int upTo) const {
        return std::numeric_limits<float>::max();
    }

    /**
     * Shallow advance to doc >= target
     * Cheaper than advance(), doesn't position for scoring
     */
    virtual int advanceShallow(int target) {
        return advance(target);
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
