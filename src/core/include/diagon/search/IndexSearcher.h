// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/ScoreMode.h"

#include <memory>

namespace diagon {

// Forward declarations
namespace index {
class IndexReader;
}  // namespace index

namespace search {

/**
 * IndexSearcher executes queries against an IndexReader.
 *
 * Based on: org.apache.lucene.search.IndexSearcher
 *
 * NOTE: Stub implementation - provides interface only.
 * Full implementation requires:
 * - Collector framework
 * - TopDocs result aggregation
 * - Multi-segment execution
 * - Query rewriting
 * - Statistics collection
 */
class IndexSearcher {
public:
    explicit IndexSearcher(index::IndexReader& reader)
        : reader_(reader) {}

    // ==================== Search Methods (Stubs) ====================

    /**
     * Count matching docs (optimized, no scoring)
     *
     * NOTE: Stub - always returns 0
     */
    int count(const Query& query) {
        // TODO: Implement full query execution
        // Requires:
        // - Query rewriting
        // - Weight creation
        // - Segment iteration
        return 0;
    }

    /**
     * Get underlying reader
     */
    index::IndexReader& getIndexReader() {
        return reader_;
    }

    const index::IndexReader& getIndexReader() const {
        return reader_;
    }

private:
    index::IndexReader& reader_;
};

}  // namespace search
}  // namespace diagon
