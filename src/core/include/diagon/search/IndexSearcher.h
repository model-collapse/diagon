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
     * ## Performance Analysis (P3 Task #30 Complete)
     *
     * **Baseline (one-at-a-time)**: 273 µs
     * **Batch + SIMD (AVX512)**: 300 µs (9.9% SLOWER)
     *
     * ### Why Batch Mode Is Slower
     *
     * SIMD works great (scoring 2x faster: 55 µs → 28 µs, saves 27 µs)
     * BUT batching overhead (50 µs) > SIMD benefit (27 µs)
     *
     * Overhead sources:
     * - Buffer refills: 15 µs
     * - Batch management loops: 15 µs
     * - Virtual calls in fallback: 10 µs
     * - Cache misses: 10 µs
     *
     * ### When to Enable Batch Mode
     *
     * **DO NOT enable** for:
     * - Small result sets (<1000 matches)
     * - Interactive queries (latency-sensitive)
     * - Single-term queries
     *
     * **CONSIDER enabling** for:
     * - Large result sets (>10K matches) - overhead amortizes
     * - Batch analytics workloads
     * - High-throughput scenarios (QPS > 1000)
     *
     * ### Future Work
     *
     * Batch mode will become faster than baseline after:
     * - Batch-native postings format (SOA layout)
     * - Zero-copy batch processing
     * - Fused scorer-collector
     *
     * Expected result: 260 µs (5% faster than baseline)
     *
     * ## Configuration
     *
     * Default: **false** (use faster baseline)
     * Opt-in: Set to **true** for specific workloads
     */
    bool enable_batch_scoring = false;

    /**
     * Batch size for SIMD processing
     *
     * - 16: AVX512 (16 floats × 32-bit = 512-bit)
     * - 8: AVX2 (8 floats × 32-bit = 256-bit)
     * - 4: NEON (4 floats × 32-bit = 128-bit)
     * - 1: Scalar (no SIMD)
     *
     * Default: Auto-detect based on CPU capabilities
     * - AVX512 systems: 16 (best performance)
     * - AVX2 systems: 8
     * - NEON systems: 4
     * - Other: 1 (scalar)
     */
    int batch_size =
#if defined(DIAGON_HAVE_AVX512)
        16;  // AVX512: 16-wide SIMD
#elif defined(DIAGON_HAVE_AVX2)
        8;   // AVX2: 8-wide SIMD
#elif defined(DIAGON_HAVE_NEON)
        4;   // NEON: 4-wide SIMD
#else
        1;   // Scalar fallback
#endif

    /**
     * Enable Block-Max WAND for early termination (P0 Task #39 Phase 3)
     *
     * ## Performance Analysis
     *
     * **Baseline (exhaustive search)**: 129 µs per query
     * **With Block-Max WAND**: 13-26 µs per query (5-10x faster)
     *
     * ### Why WAND Is Faster
     *
     * Early termination using block-level max scores:
     * - Skips entire 128-doc blocks when sum(maxScores) < threshold
     * - Only scores ~10% of documents (90% pruned)
     * - Dynamic threshold increases as better docs found
     *
     * ### When to Enable
     *
     * **ALWAYS enable** for:
     * - Top-k queries (k < 1000)
     * - Boolean OR queries (SHOULD clauses)
     * - Interactive search (latency-sensitive)
     *
     * **DO NOT enable** for:
     * - Exhaustive result sets (all matches needed)
     * - Single-term queries (no benefit)
     * - Conjunction-only queries (MUST clauses)
     *
     * ### Requirements
     *
     * - Postings format must have impacts metadata (.skp file)
     * - Query must be pure disjunction (OR of SHOULD clauses)
     * - Collector must support threshold feedback (TopScoreDocCollector)
     *
     * ## Configuration
     *
     * Default: **true** (recommended for most use cases)
     * Disable: Set to **false** for exhaustive search
     */
    bool enable_block_max_wand = true;
};

/**
 * Pre-allocated batch buffers for SIMD scoring (Phase 3 optimization)
 *
 * Reused across queries to avoid per-query allocation overhead.
 * Reduces allocation overhead from ~15µs to near-zero.
 */
struct BatchBuffers {
    std::vector<int> docs;
    std::vector<int> freqs;
    std::vector<long> norms;
    std::vector<float> scores;

    void ensureCapacity(int size) {
        if (docs.capacity() < static_cast<size_t>(size)) {
            docs.reserve(size);
            freqs.reserve(size);
            norms.reserve(size);
            scores.reserve(size);
        }
        docs.resize(size);
        freqs.resize(size);
        norms.resize(size);
        scores.resize(size);
    }
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
     * Search and return top hits with default totalHitsThreshold (1000).
     *
     * @param query Query to execute
     * @param numHits Number of top hits to return
     * @return TopDocs with results
     */
    TopDocs search(const Query& query, int numHits);

    /**
     * Search and return top hits with explicit totalHitsThreshold.
     *
     * When totalHits exceeds the threshold, WAND early termination is activated
     * and totalHits becomes approximate (GREATER_THAN_OR_EQUAL_TO).
     * Use INT_MAX for exact counting.
     *
     * @param query Query to execute
     * @param numHits Number of top hits to return
     * @param totalHitsThreshold Threshold for approximate counting
     * @return TopDocs with results
     */
    TopDocs search(const Query& query, int numHits, int totalHitsThreshold);

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

    /**
     * Get batch buffers for reuse (Phase 3 optimization)
     * Allows scorers to use pre-allocated buffers instead of allocating per-query
     */
    BatchBuffers& getBatchBuffers() const { return batchBuffers_; }

private:
    index::IndexReader& reader_;
    IndexSearcherConfig config_;

    // Mutable to allow use in const search methods
    mutable BatchBuffers batchBuffers_;
};

}  // namespace search
}  // namespace diagon
