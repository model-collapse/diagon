// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <memory>

namespace diagon {
namespace search {

// Forward declaration
class DocIdSetIterator;

/**
 * Set of document IDs
 *
 * Can be represented as:
 * - BitSet (dense)
 * - Sorted array (sparse)
 * - Iterator (streaming)
 *
 * Based on: org.apache.lucene.search.DocIdSet
 *
 * NOTE: Stub implementation - provides interface only.
 * Concrete implementations (BitSetDocIdSet, IntArrayDocIdSet) not yet implemented.
 */
class DocIdSet {
public:
    virtual ~DocIdSet() = default;

    /**
     * Get iterator over doc IDs
     */
    virtual std::unique_ptr<DocIdSetIterator> iterator() const = 0;

    /**
     * Estimated RAM usage in bytes
     */
    virtual size_t ramBytesUsed() const {
        return 0;  // Stub
    }

    /**
     * Is this a cacheable DocIdSet?
     * Some implementations may be too expensive to cache
     */
    virtual bool isCacheable() const { return true; }
};

}  // namespace search
}  // namespace diagon
