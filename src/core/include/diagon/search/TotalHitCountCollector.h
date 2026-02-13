// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Collector.h"
#include "diagon/search/Weight.h"

#include <memory>

namespace diagon {
namespace search {

/**
 * Collector that counts total matching documents without scoring.
 *
 * Uses Weight::count() per segment for O(1) counting when available
 * (e.g., TermQuery without deletions). Falls back to document-by-document
 * counting when metadata-based counting is not possible.
 *
 * Based on: org.apache.lucene.search.TotalHitCountCollector
 *
 * Usage:
 * ```cpp
 * TotalHitCountCollector collector;
 * collector.setWeight(weight.get());
 * searcher.search(query, &collector);
 * int totalHits = collector.getTotalHits();
 * ```
 */
class TotalHitCountCollector : public Collector {
public:
    TotalHitCountCollector() : totalHits_(0), weight_(nullptr) {}

    /**
     * Set the weight for sub-linear counting via Weight::count().
     * Must be called before search() if O(1) counting is desired.
     */
    void setWeight(const Weight* weight) { weight_ = weight; }

    LeafCollector* getLeafCollector(const index::LeafReaderContext& context) override {
        // Try sub-linear counting via Weight::count()
        if (weight_) {
            int leafCount = weight_->count(context);
            if (leafCount >= 0) {
                totalHits_ += leafCount;
                return nullptr;  // Skip this segment entirely
            }
        }
        // Fall back to document-by-document counting
        leafCollector_ = std::make_unique<CountingLeafCollector>(this);
        return leafCollector_.get();
    }

    ScoreMode scoreMode() const override { return ScoreMode::COMPLETE_NO_SCORES; }

    int getTotalHits() const { return totalHits_; }

private:
    class CountingLeafCollector : public LeafCollector {
    public:
        explicit CountingLeafCollector(TotalHitCountCollector* parent)
            : parent_(parent) {}

        void setScorer(Scorable* /*scorer*/) override {
            // No-op: we don't need scores
        }

        void collect(int /*doc*/) override {
            parent_->totalHits_++;
        }

    private:
        TotalHitCountCollector* parent_;
    };

    int totalHits_;
    const Weight* weight_;
    std::unique_ptr<CountingLeafCollector> leafCollector_;
};

}  // namespace search
}  // namespace diagon
