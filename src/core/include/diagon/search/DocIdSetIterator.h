// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <limits>

namespace diagon {
namespace search {

/**
 * Iterator over doc IDs
 *
 * Based on: org.apache.lucene.search.DocIdSetIterator
 *
 * NOTE: Stub implementation - provides interface only.
 * Full implementation requires:
 * - Postings format integration (Task not yet implemented)
 * - Skip list support for advance()
 * - Block-based iteration
 */
class DocIdSetIterator {
public:
    static constexpr int NO_MORE_DOCS = std::numeric_limits<int>::max();

    virtual ~DocIdSetIterator() = default;

    /**
     * Current doc ID
     * Returns -1 before first nextDoc()/advance()
     * Returns NO_MORE_DOCS when exhausted
     */
    virtual int docID() const = 0;

    /**
     * Advance to next doc
     * @return next doc ID or NO_MORE_DOCS
     */
    virtual int nextDoc() = 0;

    /**
     * Advance to doc >= target
     * @param target Minimum doc ID
     * @return doc ID >= target or NO_MORE_DOCS
     */
    virtual int advance(int target) = 0;

    /**
     * Estimated cost (number of docs)
     * Used for query optimization
     */
    virtual int64_t cost() const = 0;

    /**
     * Reset iterator to initial state
     * Default implementation does nothing (for compatibility)
     */
    virtual void reset() {}
};

}  // namespace search
}  // namespace diagon
