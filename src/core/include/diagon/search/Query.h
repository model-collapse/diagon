// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/ScoreMode.h"

#include <memory>
#include <string>

namespace diagon {

// Forward declarations
namespace index {
class IndexReader;
}  // namespace index

namespace search {

// Forward declarations
class Weight;
class IndexSearcher;

/**
 * Query is the abstract base for all queries.
 *
 * Queries are immutable and reusable.
 * createWeight() compiles query for a specific IndexSearcher.
 *
 * Based on: org.apache.lucene.search.Query
 *
 * NOTE: Stub implementation - provides interface only.
 * Concrete query implementations (TermQuery, BooleanQuery, etc.) not yet implemented.
 */
class Query {
public:
    virtual ~Query() = default;

    // ==================== Weight Creation ====================

    /**
     * Create weight for this query
     * @param searcher IndexSearcher executing the query
     * @param scoreMode How scores will be consumed
     * @param boost Boost factor for scores
     * @return Weight for execution
     */
    virtual std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                                 float boost) const = 0;

    // ==================== Rewriting ====================

    /**
     * Rewrite query for optimization
     * @param reader IndexReader for statistics
     * @return Rewritten query (may be this)
     */
    virtual std::unique_ptr<Query> rewrite(index::IndexReader& reader) const { return clone(); }

    // ==================== Utilities ====================

    /**
     * String representation
     */
    virtual std::string toString(const std::string& field) const = 0;

    /**
     * Query equality
     */
    virtual bool equals(const Query& other) const = 0;

    /**
     * Hash code for caching
     */
    virtual size_t hashCode() const = 0;

    /**
     * Clone query
     */
    virtual std::unique_ptr<Query> clone() const = 0;

protected:
    /**
     * Helper: combine boost values
     */
    static float combineBoost(float boost1, float boost2) { return boost1 * boost2; }
};

}  // namespace search
}  // namespace diagon
