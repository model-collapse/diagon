// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/ScoreMode.h"
#include "diagon/search/TopDocs.h"

#include <limits>
#include <memory>

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
 * IndexSearcher configuration
 */
struct IndexSearcherConfig {
    /**
     * Enable batch-at-a-time scoring (P1 optimization)
     *
     * When enabled, uses batch postings decoding and SIMD BM25 scoring
     * to eliminate one-at-a-time iterator overhead.
     *
     * Phase 4 analysis showed:
     * - One-at-a-time: 32.79% virtual call overhead
     * - Batch-at-a-time: Expected +19% search improvement
     *
     * Default: false (conservative, keeps existing behavior)
     */
    bool enable_batch_scoring = false;

    /**
     * Batch size for SIMD processing
     *
     * - 8: AVX2 (8 floats × 32-bit = 256-bit)
     * - 16: AVX512 (16 floats × 32-bit = 512-bit)
     *
     * Default: 8 (AVX2, widely available)
     */
    int batch_size = 8;
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
 * Phase 5 (P1) - Batch-at-a-Time Scoring:
 * - Optional batch processing mode (enable_batch_scoring)
 * - Eliminates one-at-a-time iterator overhead
 * - SIMD BM25 scoring with AVX2
 * - Expected +19% improvement when enabled
 *
 * Based on: org.apache.lucene.search.IndexSearcher
 *
 * Usage:
 * ```cpp
 * auto reader = DirectoryReader::open(*directory);
 *
 * // Default mode (one-at-a-time)
 * IndexSearcher searcher(*reader);
 *
 * // Batch mode (P1 optimization)
 * IndexSearcherConfig config;
 * config.enable_batch_scoring = true;
 * IndexSearcher batchSearcher(*reader, config);
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
        : reader_(reader), config_() {}

    explicit IndexSearcher(index::IndexReader& reader, const IndexSearcherConfig& config)
        : reader_(reader), config_(config) {}

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
    index::IndexReader& getIndexReader() { return reader_; }

    const index::IndexReader& getIndexReader() const { return reader_; }

    /**
     * Get configuration
     */
    const IndexSearcherConfig& getConfig() const { return config_; }

private:
    index::IndexReader& reader_;
    IndexSearcherConfig config_;
};

}  // namespace search
}  // namespace diagon
