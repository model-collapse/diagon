// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/LeafReaderContext.h"
#include "diagon/search/Collector.h"
#include "diagon/search/TopDocs.h"

#include <memory>
#include <queue>
#include <vector>

namespace diagon {
namespace search {

/**
 * Collector that collects top-scoring documents.
 *
 * Uses a priority queue to maintain the top-K results sorted by score
 * descending. Supports pagination via searchAfter.
 *
 * Based on: org.apache.lucene.search.TopScoreDocCollector
 *
 * Usage:
 * ```cpp
 * // Get top 10 results
 * auto collector = TopScoreDocCollector::create(10);
 * searcher.search(query, collector.get());
 * TopDocs results = collector->topDocs();
 *
 * // Pagination: get next 10 results after last result
 * auto after = results.scoreDocs.back();
 * auto nextCollector = TopScoreDocCollector::create(10, after);
 * searcher.search(query, nextCollector.get());
 * TopDocs nextResults = nextCollector->topDocs();
 * ```
 */
class TopScoreDocCollector : public Collector {
public:
    /**
     * Create collector for top-K results.
     *
     * @param numHits Number of top hits to collect
     * @return Collector instance
     */
    static std::unique_ptr<TopScoreDocCollector> create(int numHits);

    /**
     * Create collector for top-K results after a given doc (pagination).
     *
     * @param numHits Number of top hits to collect
     * @param after Only collect hits after this doc (searchAfter)
     * @return Collector instance
     */
    static std::unique_ptr<TopScoreDocCollector> create(int numHits, const ScoreDoc& after);

    /**
     * Get the collected top documents.
     *
     * @return TopDocs with top hits
     */
    TopDocs topDocs();

    /**
     * Get the collected top documents with custom start offset.
     *
     * @param start Start offset in results
     * @param howMany Number of results to return
     * @return TopDocs with specified range
     */
    TopDocs topDocs(int start, int howMany);

    // ==================== Collector Interface ====================

    LeafCollector* getLeafCollector(const index::LeafReaderContext& context) override;

    ScoreMode scoreMode() const override { return ScoreMode::COMPLETE; }

private:
    /**
     * Private constructor (use create() factory methods)
     */
    TopScoreDocCollector(int numHits, const ScoreDoc* after);

    /**
     * Internal leaf collector for a single segment
     */
    class TopScoreLeafCollector : public LeafCollector {
    public:
        TopScoreLeafCollector(TopScoreDocCollector* parent,
                              const index::LeafReaderContext& context);

        ~TopScoreLeafCollector();

        void setScorer(Scorable* scorer) override { scorer_ = scorer; }

        void collect(int doc) override;

        void finishSegment();  // Flush remaining batch

    private:
        void collectSingle(int doc, float score);
        void flushBatch();

#if defined(DIAGON_HAVE_AVX512)
        static constexpr int BATCH_SIZE = 16;  // AVX512: 16 floats
        alignas(64) int docBatch_[BATCH_SIZE];
        alignas(64) float scoreBatch_[BATCH_SIZE];
        int batchPos_;
#elif defined(DIAGON_HAVE_AVX2)
        static constexpr int BATCH_SIZE = 8;  // AVX2: 8 floats
        alignas(32) int docBatch_[BATCH_SIZE];
        alignas(32) float scoreBatch_[BATCH_SIZE];
        int batchPos_;
#endif

        TopScoreDocCollector* parent_;
        int docBase_;
        Scorable* scorer_;
        const ScoreDoc* after_;
    };

    friend class TopScoreLeafCollector;

    // ==================== Internal State ====================

    // Comparator for priority queue: creates heap where .top() returns worst document
    // This allows us to efficiently reject docs worse than the worst in top-K
    struct ScoreDocComparator {
        bool operator()(const ScoreDoc& a, const ScoreDoc& b) const {
            // Return true if 'a' should be higher in the heap than 'b'
            // We want worst docs at top, so we can efficiently reject worse docs
            if (a.score != b.score) {
                return a.score > b.score;  // Higher score → higher in heap (further from top)
            }
            // For equal scores, higher doc ID is higher in heap (further from top)
            return a.doc < b.doc;
        }
    };

    int numHits_;                            // Number of hits to collect
    ScoreDoc after_;                         // For searchAfter pagination
    bool hasAfter_;                          // Whether after_ is set
    int64_t totalHits_;                      // Total matching documents
    TotalHits::Relation totalHitsRelation_;  // Relation (exact or lower bound)

    // Priority queue: .top() returns worst document in top-K set
    // When queue is full, we can reject docs with score <= top
    std::priority_queue<ScoreDoc, std::vector<ScoreDoc>, ScoreDocComparator> pq_;

    // Leaf collector instance (reused across segments)
    std::unique_ptr<TopScoreLeafCollector> leafCollector_;
};

}  // namespace search
}  // namespace diagon
