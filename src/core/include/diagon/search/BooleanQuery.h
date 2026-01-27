// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/BooleanClause.h"
#include "diagon/search/Query.h"
#include "diagon/search/Weight.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace search {

/**
 * BooleanQuery - Combines multiple sub-queries with boolean logic
 *
 * Supports four clause types:
 * - MUST: Required, participates in scoring (AND for relevance)
 * - SHOULD: Optional, participates in scoring (OR for relevance)
 * - MUST_NOT: Prohibited, no scoring (NOT filter)
 * - FILTER: Required, no scoring (AND filter)
 *
 * Scoring:
 * - Sum of MUST and SHOULD clause scores
 * - FILTER and MUST_NOT don't contribute to score
 *
 * Examples:
 *   // Search: (category:electronics AND in_stock:true) OR featured:true
 *   BooleanQuery::Builder()
 *       .add(termQuery("category", "electronics"), Occur::MUST)
 *       .add(termQuery("in_stock", "true"), Occur::FILTER)
 *       .add(termQuery("featured", "true"), Occur::SHOULD)
 *       .setMinimumNumberShouldMatch(0)
 *       .build()
 *
 *   // Filter: price >= 100 AND category = electronics
 *   BooleanQuery::Builder()
 *       .add(rangeQuery("price", 100, MAX), Occur::FILTER)
 *       .add(termQuery("category", "electronics"), Occur::FILTER)
 *       .build()
 *
 * Based on: org.apache.lucene.search.BooleanQuery
 *
 * Phase 4 implementation:
 * - Basic AND/OR/NOT logic
 * - Sum scoring for MUST/SHOULD
 * - minimumNumberShouldMatch support
 * - No coord factor (removed in Lucene 8)
 * - No query boosting per clause (use BoostQuery wrapper)
 * - No two-phase iteration (Phase 5)
 * - No WAND optimization (Phase 5)
 */
class BooleanQuery : public Query {
public:
    /**
     * Builder for constructing BooleanQuery instances
     */
    class Builder {
    public:
        Builder();

        /**
         * Add a clause to the query
         */
        Builder& add(std::shared_ptr<Query> query, Occur occur);

        /**
         * Add a clause from BooleanClause
         */
        Builder& add(const BooleanClause& clause);

        /**
         * Set minimum number of SHOULD clauses that must match
         *
         * 0 = at least one SHOULD must match if no MUST clauses
         * N = at least N SHOULD clauses must match
         */
        Builder& setMinimumNumberShouldMatch(int min);

        /**
         * Build the query
         */
        std::unique_ptr<BooleanQuery> build();

    private:
        std::vector<BooleanClause> clauses_;
        int minimumNumberShouldMatch_;
    };

    // ==================== Accessors ====================

    /**
     * Get all clauses
     */
    const std::vector<BooleanClause>& clauses() const { return clauses_; }

    /**
     * Get minimum number of SHOULD clauses that must match
     */
    int getMinimumNumberShouldMatch() const { return minimumNumberShouldMatch_; }

    /**
     * Check if query is pure disjunction (only SHOULD clauses)
     */
    bool isPureDisjunction() const;

    /**
     * Check if query is required (has MUST or FILTER clauses)
     */
    bool isRequired() const;

    // ==================== Query Interface ====================

    std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                         float boost) const override;

    std::unique_ptr<Query> rewrite(index::IndexReader& reader) const override;

    std::string toString(const std::string& field) const override;

    bool equals(const Query& other) const override;

    size_t hashCode() const override;

    std::unique_ptr<Query> clone() const override;

private:
    /**
     * Private constructor - use Builder
     */
    BooleanQuery(std::vector<BooleanClause> clauses, int minimumNumberShouldMatch);

    std::vector<BooleanClause> clauses_;
    int minimumNumberShouldMatch_;
};

}  // namespace search
}  // namespace diagon
