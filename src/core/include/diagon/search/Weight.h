// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/BulkScorer.h"
#include "diagon/search/Scorer.h"

#include <memory>
#include <string>

namespace diagon {

// Forward declarations
namespace index {
struct LeafReaderContext;
}  // namespace index

namespace search {

// Forward declarations
class Query;

/**
 * Weight is the compiled form of a Query.
 *
 * Contains statistics and can create Scorers for segments.
 * One Weight per IndexSearcher, reusable across segments.
 *
 * Based on: org.apache.lucene.search.Weight
 *
 * NOTE: Stub implementation - provides interface only.
 * Full implementation requires:
 * - Collection/term statistics
 * - Scorer creation
 * - Score explanation
 */
class Weight {
public:
    virtual ~Weight() = default;

    // ==================== Scorer Creation ====================

    /**
     * Create scorer for segment
     * @param context Segment to search
     * @return Scorer or nullptr if no matches possible
     */
    virtual std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const = 0;

    /**
     * Create a BulkScorer for segment-level batch scoring.
     *
     * BulkScorer drives iteration internally, processing documents in
     * 4096-doc windows with essential/non-essential clause partitioning.
     * Returns nullptr if BulkScorer not available (falls back to scorer()).
     *
     * BooleanWeight overrides this for pure disjunctions to return
     * MaxScoreBulkScorer, which is significantly faster than doc-at-a-time WAND.
     *
     * Based on: org.apache.lucene.search.Weight.bulkScorer(LeafReaderContext)
     *
     * @param context Segment to search
     * @return BulkScorer or nullptr to use scorer() fallback
     */
    virtual std::unique_ptr<BulkScorer> bulkScorer(const index::LeafReaderContext& context) const {
        return nullptr;
    }

    // ==================== Statistics ====================

    /**
     * Is this weight cacheable for the given segment?
     */
    virtual bool isCacheable(const index::LeafReaderContext& context) const { return true; }

    /**
     * Optionally return the count of matching documents in sub-linear time.
     * Returns -1 if count cannot be computed without iterating.
     * TermQuery overrides this to return docFreq() in O(1) when no deletions.
     *
     * Based on: org.apache.lucene.search.Weight.count(LeafReaderContext)
     */
    virtual int count(const index::LeafReaderContext& context) const { return -1; }

    // ==================== Utilities ====================

    /**
     * Get parent query
     */
    virtual const Query& getQuery() const = 0;

    /**
     * String representation for debugging
     */
    virtual std::string toString() const { return "Weight"; }
};

}  // namespace search
}  // namespace diagon
