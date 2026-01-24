// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/IndexSearcher.h"

#include "diagon/index/IndexReader.h"
#include "diagon/search/Collector.h"
#include "diagon/search/Scorer.h"
#include "diagon/search/TopDocs.h"
#include "diagon/search/TopScoreDocCollector.h"
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

private:
    Scorer* scorer_;
};

// ==================== IndexSearcher Implementation ====================

TopDocs IndexSearcher::search(const Query& query, int numHits) {
    // Create collector
    auto collector = TopScoreDocCollector::create(numHits);

    // Execute search
    search(query, collector.get());

    // Get results
    return collector->topDocs();
}

void IndexSearcher::search(const Query& query, Collector* collector) {
    // Phase 4: Simplified - no query rewriting
    // TODO Phase 5: Implement query rewriting

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

        // Create scorer for this segment
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
    }
}

int IndexSearcher::count(const Query& query) {
    // Phase 4: Simple implementation using search
    // TODO Phase 5: Optimize with dedicated counting (no scoring)
    auto results = search(query, Integer::MAX_VALUE);
    return static_cast<int>(results.totalHits.value);
}

}  // namespace search
}  // namespace diagon
