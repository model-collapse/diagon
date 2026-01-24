// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/TopScoreDocCollector.h"

#include <algorithm>
#include <cmath>

namespace diagon {
namespace search {

// ==================== Factory Methods ====================

std::unique_ptr<TopScoreDocCollector> TopScoreDocCollector::create(int numHits) {
    return std::unique_ptr<TopScoreDocCollector>(new TopScoreDocCollector(numHits, nullptr));
}

std::unique_ptr<TopScoreDocCollector> TopScoreDocCollector::create(int numHits,
                                                                   const ScoreDoc& after) {
    return std::unique_ptr<TopScoreDocCollector>(new TopScoreDocCollector(numHits, &after));
}

// ==================== Constructor ====================

TopScoreDocCollector::TopScoreDocCollector(int numHits, const ScoreDoc* after)
    : numHits_(numHits)
    , after_(after ? *after : ScoreDoc())
    , hasAfter_(after != nullptr)
    , totalHits_(0)
    , totalHitsRelation_(TotalHits::Relation::EQUAL_TO)
    , pq_()
    , leafCollector_(nullptr) {
    if (numHits <= 0) {
        throw std::invalid_argument("numHits must be > 0");
    }
}

// ==================== Collector Implementation ====================

LeafCollector* TopScoreDocCollector::getLeafCollector(const index::LeafReaderContext& context) {
    leafCollector_ = std::make_unique<TopScoreLeafCollector>(this, context);
    return leafCollector_.get();
}

// ==================== TopDocs Retrieval ====================

TopDocs TopScoreDocCollector::topDocs() {
    return topDocs(0, numHits_);
}

TopDocs TopScoreDocCollector::topDocs(int start, int howMany) {
    if (start < 0 || howMany < 0) {
        throw std::invalid_argument("start and howMany must be >= 0");
    }

    // Convert priority queue to vector
    // Heap has worst document at top, so extraction gives worst-to-best order
    std::vector<ScoreDoc> results;
    results.reserve(pq_.size());

    while (!pq_.empty()) {
        results.push_back(pq_.top());
        pq_.pop();
    }

    // Reverse to get best-to-worst order (highest score first, then lowest doc ID for ties)
    std::reverse(results.begin(), results.end());

    // Apply start/howMany slicing
    if (start >= static_cast<int>(results.size())) {
        results.clear();
    } else {
        int end = std::min(start + howMany, static_cast<int>(results.size()));
        results = std::vector<ScoreDoc>(results.begin() + start, results.begin() + end);
    }

    TotalHits hits(totalHits_, totalHitsRelation_);
    return TopDocs(hits, results);
}

// ==================== TopScoreLeafCollector ====================

TopScoreDocCollector::TopScoreLeafCollector::TopScoreLeafCollector(
    TopScoreDocCollector* parent, const index::LeafReaderContext& context)
    : parent_(parent)
    , docBase_(context.docBase)
    , scorer_(nullptr)
    , after_(parent->hasAfter_ ? &parent->after_ : nullptr) {}

void TopScoreDocCollector::TopScoreLeafCollector::collect(int doc) {
    if (!scorer_) {
        throw std::runtime_error("Scorer not set");
    }

    float score = scorer_->score();
    parent_->totalHits_++;

    // Skip NaN and infinite scores (invalid) - don't add to results but count in totalHits
    // Note: -ffast-math is disabled for this file to allow proper NaN/inf checking
    if (std::isnan(score) || std::isinf(score)) {
        return;
    }

    // Global doc ID
    int globalDoc = docBase_ + doc;

    // Check if this doc is after the pagination point
    if (after_ != nullptr) {
        // Skip docs that come before 'after'
        // after_.doc is global, doc is local to segment
        if (globalDoc < after_->doc) {
            return;
        }
        if (globalDoc == after_->doc) {
            return;  // Skip the 'after' doc itself
        }
        // If scores are equal, use doc ID for tie-breaking
        if (score == after_->score && globalDoc <= after_->doc) {
            return;
        }
    }

    // Add to priority queue
    ScoreDoc scoreDoc(globalDoc, score);

    if (static_cast<int>(parent_->pq_.size()) < parent_->numHits_) {
        // Queue not full yet, just add
        parent_->pq_.push(scoreDoc);
    } else {
        // Queue is full, check if this doc beats the worst doc
        const ScoreDoc& top = parent_->pq_.top();

        // Compare: higher score is better, lower doc ID breaks ties
        bool betterThanTop = (score > top.score) || (score == top.score && globalDoc < top.doc);

        if (betterThanTop) {
            parent_->pq_.pop();
            parent_->pq_.push(scoreDoc);
        }
    }
}

}  // namespace search
}  // namespace diagon
