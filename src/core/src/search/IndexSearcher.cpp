// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/IndexSearcher.h"

#include "diagon/index/IndexReader.h"
#include "diagon/search/BulkScorer.h"
#include "diagon/search/Collector.h"
#include "diagon/search/Scorer.h"
#include "diagon/search/TopDocs.h"
#include "diagon/search/TopScoreDocCollector.h"
#include "diagon/search/TotalHitCountCollector.h"
#include "diagon/search/Weight.h"

namespace diagon {
namespace search {

// ==================== Scorable Adapter ====================

/**
 * Adapter to make Scorer implement Scorable interface
 */
class ScorerScorable : public Scorable {
public:
    explicit ScorerScorable(Scorer* scorer)
        : scorer_(scorer) {}

    float score() override { return scorer_->score(); }

    int docID() override { return scorer_->docID(); }

    void setMinCompetitiveScore(float minScore) override {
        // Forward to underlying scorer (P0 Task #39: WAND threshold feedback)
        scorer_->setMinCompetitiveScore(minScore);
    }

    int getTotalMatches() const override {
        // Forward to underlying scorer (WAND totalHits fix)
        return scorer_->getTotalMatches();
    }

private:
    Scorer* scorer_;
};

// ==================== IndexSearcher Implementation ====================

TopDocs IndexSearcher::search(const Query& query, int numHits) {
    return search(query, numHits, 1000);
}

TopDocs IndexSearcher::search(const Query& query, int numHits, int totalHitsThreshold) {
    // Create collector with threshold for WAND early termination
    auto collector = TopScoreDocCollector::create(numHits, totalHitsThreshold);

    // Execute search
    search(query, collector.get());

    // Get results
    return collector->topDocs();
}

void IndexSearcher::search(const Query& query, Collector* collector) {
    // Create weight for query
    auto weight = query.createWeight(*this, collector->scoreMode(), 1.0f);

    // Get leaf contexts (segments)
    auto leaves = reader_.leaves();

    // Execute query on each segment
    for (const auto& ctx : leaves) {
        // Get leaf collector for this segment
        auto leafCollector = collector->getLeafCollector(ctx);
        if (!leafCollector) {
            continue;  // Collector doesn't want this segment
        }

        // Try BulkScorer first (window-based, significantly faster for disjunctions)
        auto bulkScorer = weight->bulkScorer(ctx);
        if (bulkScorer) {
            // BulkScorer drives iteration internally and calls collector.collect()
            bulkScorer->score(leafCollector, 0, DocIdSetIterator::NO_MORE_DOCS);
            leafCollector->finishSegment();
            continue;
        }

        // Fallback: doc-at-a-time iteration via Scorer
        auto scorer = weight->scorer(ctx);
        if (!scorer) {
            continue;  // No matches in this segment
        }

        // Set scorer on leaf collector
        ScorerScorable scorable(scorer.get());
        leafCollector->setScorer(&scorable);

        // Iterate matching documents
        int doc;
        while ((doc = scorer->nextDoc()) != DocIdSetIterator::NO_MORE_DOCS) {
            leafCollector->collect(doc);
        }

        // Finish segment to flush any batched results (SIMD)
        leafCollector->finishSegment();
    }
}

int IndexSearcher::count(const Query& query) {
    // Create weight with COMPLETE_NO_SCORES (no scoring needed)
    auto weight = query.createWeight(*this, ScoreMode::COMPLETE_NO_SCORES, 1.0f);

    // Try to count using Weight::count() per segment (O(1) for TermQuery)
    int totalCount = 0;
    bool allSegmentsCounted = true;
    auto leaves = reader_.leaves();

    for (const auto& ctx : leaves) {
        int leafCount = weight->count(ctx);
        if (leafCount >= 0) {
            totalCount += leafCount;
        } else {
            allSegmentsCounted = false;
            break;
        }
    }

    if (allSegmentsCounted) {
        return totalCount;  // O(1) fast path
    }

    // Fallback: use TotalHitCountCollector (iterates but doesn't score)
    TotalHitCountCollector collector;
    collector.setWeight(weight.get());
    search(query, &collector);
    return collector.getTotalHits();
}

}  // namespace search
}  // namespace diagon
