// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Scorer.h"

#include <memory>
#include <string>

namespace diagon {

// Forward declarations
namespace index {
class LeafReaderContext;
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

    // ==================== Statistics ====================

    /**
     * Is this weight cacheable for the given segment?
     */
    virtual bool isCacheable(const index::LeafReaderContext& context) const {
        return true;
    }

    // ==================== Utilities ====================

    /**
     * Get parent query
     */
    virtual const Query& getQuery() const = 0;

    /**
     * String representation for debugging
     */
    virtual std::string toString() const {
        return "Weight";
    }
};

}  // namespace search
}  // namespace diagon
