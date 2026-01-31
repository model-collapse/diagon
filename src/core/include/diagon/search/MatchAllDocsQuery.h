// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/Weight.h"
#include "diagon/search/Scorer.h"

namespace diagon {
namespace search {

/**
 * Simple MatchAllQuery implementation that matches all documents
 * Returns a constant score of 1.0 for all documents
 */
class MatchAllQuery : public Query {
public:
    MatchAllQuery() = default;

    std::unique_ptr<Query> clone() const override {
        return std::make_unique<MatchAllQuery>();
    }

    std::string toString(const std::string& field) const override {
        return "*:*";
    }

    std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                         float boost) const override;

    bool equals(const Query& other) const override {
        // All MatchAllQuery instances are equal
        return dynamic_cast<const MatchAllQuery*>(&other) != nullptr;
    }

    size_t hashCode() const override {
        // All MatchAllQuery instances have same hash
        return 0;
    }
};

/**
 * Weight implementation for MatchAllQuery
 */
class MatchAllWeight : public Weight {
private:
    const Query* query_;
    float boost_;

public:
    MatchAllWeight(const Query* query, float boost)
        : query_(query), boost_(boost) {}

    std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const override;

    const Query& getQuery() const override {
        return *query_;
    }
};

/**
 * Scorer implementation for MatchAllQuery
 * Simply iterates through all document IDs
 */
class MatchAllScorer : public Scorer {
private:
    const Weight* weight_;
    int32_t maxDoc_;
    mutable int32_t currentDoc_;
    float score_;

public:
    MatchAllScorer(const Weight* weight, int32_t maxDoc, float score)
        : weight_(weight), maxDoc_(maxDoc), currentDoc_(-1), score_(score) {}

    int32_t docID() const override {
        return currentDoc_;
    }

    int32_t nextDoc() override {
        currentDoc_++;
        if (currentDoc_ >= maxDoc_) {
            currentDoc_ = NO_MORE_DOCS;
        }
        return currentDoc_;
    }

    int32_t advance(int32_t target) override {
        if (target >= maxDoc_) {
            currentDoc_ = NO_MORE_DOCS;
        } else {
            currentDoc_ = target;
        }
        return currentDoc_;
    }

    int64_t cost() const override {
        return maxDoc_;
    }

    float score() const override {
        return score_;
    }

    const Weight& getWeight() const override {
        return *weight_;
    }

    float getMaxScore(int upTo) const override {
        return score_;
    }
};

} // namespace search
} // namespace diagon

