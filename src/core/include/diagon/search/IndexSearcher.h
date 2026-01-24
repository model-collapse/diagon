// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/ScoreMode.h"
#include "diagon/search/TopDocs.h"

#include <memory>
#include <limits>

namespace diagon {

// Forward declarations
namespace index {
class IndexReader;
}  // namespace index

namespace search {

// Forward declarations
class Collector;

/**
 * Helper class for integer constants
 */
struct Integer {
    static constexpr int MAX_VALUE = std::numeric_limits<int>::max();
};

/**
 * IndexSearcher executes queries against an IndexReader.
 *
 * Phase 4 implementation:
 * - Basic query execution with collectors
 * - Multi-segment coordination
 * - TopDocs result aggregation
 * - No query rewriting
 * - No caching
 *
 * Based on: org.apache.lucene.search.IndexSearcher
 *
 * Usage:
 * ```cpp
 * auto reader = DirectoryReader::open(*directory);
 * IndexSearcher searcher(*reader);
 *
 * // Search with collector
 * auto collector = TopScoreDocCollector::create(10);
 * searcher.search(*query, collector.get());
 * TopDocs results = collector->topDocs();
 *
 * // Or use convenience method
 * TopDocs results = searcher.search(*query, 10);
 * ```
 */
class IndexSearcher {
public:
    explicit IndexSearcher(index::IndexReader& reader)
        : reader_(reader) {}

    // ==================== Search Methods ====================

    /**
     * Search and return top hits.
     *
     * @param query Query to execute
     * @param numHits Number of top hits to return
     * @return TopDocs with results
     */
    TopDocs search(const Query& query, int numHits);

    /**
     * Search with custom collector.
     *
     * @param query Query to execute
     * @param collector Collector to receive hits
     */
    void search(const Query& query, Collector* collector);

    /**
     * Count matching documents (optimized, no scoring).
     *
     * @param query Query to execute
     * @return Count of matching documents
     */
    int count(const Query& query);

    // ==================== Reader Access ====================

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
