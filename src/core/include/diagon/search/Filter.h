// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>

namespace diagon {

// Forward declarations
namespace index {
struct LeafReaderContext;
}  // namespace index

namespace search {

// Forward declarations
class DocIdSet;
class DocIdSetIterator;

/**
 * Filter constrains documents without affecting scores
 *
 * Optimized for:
 * - No score computation (returns DocIdSetIterator, not Scorer)
 * - Caching (via cache key)
 * - Skip index integration
 * - Early termination
 *
 * Based on: org.apache.lucene.search.Query with scoring disabled
 *
 * NOTE: Stub implementation - provides interface only.
 * Concrete filter implementations (RangeFilter, TermFilter, etc.) not yet implemented.
 */
class Filter {
public:
    virtual ~Filter() = default;

    // ==================== DocIdSet Creation ====================

    /**
     * Get doc ID set for segment
     * @param context Segment to filter
     * @return DocIdSet or nullptr if no matches possible
     */
    virtual std::unique_ptr<DocIdSet>
    getDocIdSet(const index::LeafReaderContext& context) const = 0;

    // ==================== Caching Support ====================

    /**
     * Cache key for filter results
     * Return empty string if filter results should not be cached
     */
    virtual std::string getCacheKey() const {
        return "";  // Default: not cacheable
    }

    /**
     * Should this filter be cached?
     */
    bool isCacheable() const { return !getCacheKey().empty(); }

    // ==================== Utilities ====================

    /**
     * String representation
     */
    virtual std::string toString() const = 0;

    /**
     * Filter equality (for caching)
     */
    virtual bool equals(const Filter& other) const = 0;

    /**
     * Hash code (for caching)
     */
    virtual size_t hashCode() const = 0;
};

using FilterPtr = std::shared_ptr<Filter>;

}  // namespace search
}  // namespace diagon
