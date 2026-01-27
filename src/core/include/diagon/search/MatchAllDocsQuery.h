#ifndef MATCH_ALL_QUERY_H
#define MATCH_ALL_QUERY_H

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
    
    std::unique_ptr<Weight> createWeight(IndexSearcher* searcher, bool needsScores, float boost) override;
};

/**
 * Weight implementation for MatchAllQuery
 */
class MatchAllWeight : public Weight {
private:
    float boost_;
    
public:
    explicit MatchAllWeight(MatchAllQuery* query, float boost)
        : Weight(query), boost_(boost) {}
    
    std::unique_ptr<Scorer> scorer(index::LeafReaderContext* context) override;
    
    float getValueForNormalization() override {
        return boost_ * boost_;
    }
    
    void normalize(float norm, float boost) override {
        boost_ = norm * boost;
    }
};

/**
 * Scorer implementation for MatchAllQuery
 * Simply iterates through all document IDs
 */
class MatchAllScorer : public Scorer {
private:
    int32_t maxDoc_;
    int32_t currentDoc_;
    float score_;
    
public:
    MatchAllScorer(Weight* weight, int32_t maxDoc, float score)
        : Scorer(weight), maxDoc_(maxDoc), currentDoc_(-1), score_(score) {}
    
    int32_t docID() override {
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
    
    float score() override {
        return score_;
    }
    
    float getMaxScore() override {
        return score_;
    }
};

} // namespace search
} // namespace diagon

#endif // MATCH_ALL_QUERY_H
