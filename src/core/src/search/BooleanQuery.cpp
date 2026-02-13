// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/BooleanQuery.h"

#include "diagon/index/IndexReader.h"
#include "diagon/index/LeafReaderContext.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/MaxScoreBulkScorer.h"
#include "diagon/search/Scorer.h"
#include "diagon/search/WANDScorer.h"
#include "diagon/search/BM25Similarity.h"

#include <algorithm>
#include <sstream>

namespace diagon {
namespace search {

// ==================== ConjunctionScorer (AND) ====================

/**
 * ConjunctionScorer - Scores documents matching all sub-scorers (AND logic)
 *
 * Advances all scorers in lockstep, only scoring docs where all match.
 * Score is sum of all sub-scorer scores.
 */
class ConjunctionScorer : public Scorer {
public:
    ConjunctionScorer(const Weight& weight, std::vector<std::unique_ptr<Scorer>> scorers)
        : weight_(weight)
        , scorers_(std::move(scorers))
        , doc_(-1) {
        if (scorers_.empty()) {
            throw std::invalid_argument("ConjunctionScorer requires at least one scorer");
        }
    }

    int docID() const override { return doc_; }

    int nextDoc() override {
        if (doc_ == NO_MORE_DOCS) {
            return doc_;
        }

        // Advance primary scorer
        doc_ = scorers_[0]->nextDoc();

        // Advance all other scorers to match
        while (doc_ != NO_MORE_DOCS) {
            bool allMatch = true;

            for (size_t i = 1; i < scorers_.size(); ++i) {
                int otherDoc = scorers_[i]->docID();

                if (otherDoc < doc_) {
                    otherDoc = scorers_[i]->advance(doc_);
                }

                if (otherDoc != doc_) {
                    // This scorer doesn't have doc, advance primary
                    doc_ = scorers_[0]->advance(otherDoc);
                    allMatch = false;
                    break;
                }
            }

            if (allMatch) {
                return doc_;
            }
        }

        return NO_MORE_DOCS;
    }

    int advance(int target) override {
        if (doc_ == NO_MORE_DOCS || target >= NO_MORE_DOCS) {
            doc_ = NO_MORE_DOCS;
            return doc_;
        }

        // Advance all scorers to target
        for (auto& scorer : scorers_) {
            int scorerDoc = scorer->docID();
            if (scorerDoc < target) {
                scorerDoc = scorer->advance(target);
            }
            if (scorerDoc == NO_MORE_DOCS) {
                doc_ = NO_MORE_DOCS;
                return doc_;
            }
        }

        // Find next matching doc
        doc_ = scorers_[0]->docID();
        if (doc_ < target) {
            return nextDoc();
        }

        // Check if all match at current position
        for (size_t i = 1; i < scorers_.size(); ++i) {
            if (scorers_[i]->docID() != doc_) {
                return nextDoc();
            }
        }

        return doc_;
    }

    int64_t cost() const override {
        // Cost is minimum of all sub-scorers (AND is most selective)
        int64_t minCost = std::numeric_limits<int64_t>::max();
        for (const auto& scorer : scorers_) {
            minCost = std::min(minCost, scorer->cost());
        }
        return minCost;
    }

    float score() const override {
        // Sum all sub-scores
        float totalScore = 0.0f;
        for (const auto& scorer : scorers_) {
            totalScore += scorer->score();
        }
        return totalScore;
    }

    const Weight& getWeight() const override { return weight_; }

private:
    const Weight& weight_;
    std::vector<std::unique_ptr<Scorer>> scorers_;
    int doc_;
};

// ==================== DisjunctionScorer (OR) ====================

/**
 * DisjunctionScorer - Scores documents matching any sub-scorer (OR logic)
 *
 * Advances to minimum doc ID across all scorers.
 * Score is sum of matching scorers at current doc.
 */
class DisjunctionScorer : public Scorer {
public:
    DisjunctionScorer(const Weight& weight, std::vector<std::unique_ptr<Scorer>> scorers,
                      int minimumNumberShouldMatch)
        : weight_(weight)
        , scorers_(std::move(scorers))
        , minimumNumberShouldMatch_(minimumNumberShouldMatch)
        , doc_(-1) {
        if (scorers_.empty()) {
            throw std::invalid_argument("DisjunctionScorer requires at least one scorer");
        }
        if (minimumNumberShouldMatch_ < 1) {
            minimumNumberShouldMatch_ = 1;
        }
        if (minimumNumberShouldMatch_ > static_cast<int>(scorers_.size())) {
            doc_ = NO_MORE_DOCS;  // Impossible to match
        }
    }

    int docID() const override { return doc_; }

    int nextDoc() override {
        if (doc_ == NO_MORE_DOCS) {
            return doc_;
        }

        while (true) {
            // Find minimum doc ID across all scorers
            int minDoc = NO_MORE_DOCS;
            for (auto& scorer : scorers_) {
                int scorerDoc = scorer->docID();
                if (scorerDoc <= doc_) {
                    scorerDoc = scorer->nextDoc();
                }
                if (scorerDoc < minDoc) {
                    minDoc = scorerDoc;
                }
            }

            if (minDoc == NO_MORE_DOCS) {
                doc_ = NO_MORE_DOCS;
                return doc_;
            }

            // Count how many scorers match at minDoc
            int matchCount = 0;
            for (auto& scorer : scorers_) {
                if (scorer->docID() == minDoc) {
                    matchCount++;
                }
            }

            if (matchCount >= minimumNumberShouldMatch_) {
                doc_ = minDoc;
                return doc_;
            }

            // Not enough matches, continue to next doc
            doc_ = minDoc;
        }
    }

    int advance(int target) override {
        if (doc_ == NO_MORE_DOCS || target >= NO_MORE_DOCS) {
            doc_ = NO_MORE_DOCS;
            return doc_;
        }

        // Advance all scorers to target
        for (auto& scorer : scorers_) {
            if (scorer->docID() < target) {
                scorer->advance(target);
            }
        }

        doc_ = target - 1;  // Ensure nextDoc() starts from target
        return nextDoc();
    }

    int64_t cost() const override {
        // Cost is sum of all sub-scorers (OR is least selective)
        int64_t totalCost = 0;
        for (const auto& scorer : scorers_) {
            totalCost += scorer->cost();
        }
        return totalCost;
    }

    float score() const override {
        // Sum scores of all scorers matching at current doc
        float totalScore = 0.0f;
        for (const auto& scorer : scorers_) {
            if (scorer->docID() == doc_) {
                totalScore += scorer->score();
            }
        }
        return totalScore;
    }

    const Weight& getWeight() const override { return weight_; }

private:
    const Weight& weight_;
    std::vector<std::unique_ptr<Scorer>> scorers_;
    int minimumNumberShouldMatch_;
    int doc_;
};

// ==================== ReqExclScorer (MUST_NOT) ====================

/**
 * ReqExclScorer - Filters required scorer by excluded docs
 *
 * Advances required scorer, skipping docs that match excluded scorer.
 */
class ReqExclScorer : public Scorer {
public:
    ReqExclScorer(std::unique_ptr<Scorer> reqScorer, std::unique_ptr<Scorer> exclScorer)
        : reqScorer_(std::move(reqScorer))
        , exclScorer_(std::move(exclScorer)) {}

    int docID() const override { return reqScorer_->docID(); }

    int nextDoc() override {
        int doc = reqScorer_->nextDoc();

        while (doc != NO_MORE_DOCS) {
            int exclDoc = exclScorer_->docID();

            // Advance excluded scorer if needed
            if (exclDoc < doc) {
                exclDoc = exclScorer_->advance(doc);
            }

            // If excluded scorer matches, skip this doc
            if (exclDoc == doc) {
                doc = reqScorer_->nextDoc();
            } else {
                return doc;
            }
        }

        return NO_MORE_DOCS;
    }

    int advance(int target) override {
        int doc = reqScorer_->advance(target);

        if (doc == NO_MORE_DOCS) {
            return doc;
        }

        return nextDoc();
    }

    int64_t cost() const override { return reqScorer_->cost(); }

    float score() const override { return reqScorer_->score(); }

    const Weight& getWeight() const override { return reqScorer_->getWeight(); }

private:
    std::unique_ptr<Scorer> reqScorer_;
    std::unique_ptr<Scorer> exclScorer_;
};

// ==================== BooleanWeight ====================

/**
 * BooleanWeight - Weight for BooleanQuery
 *
 * Creates appropriate scorer based on clause types
 */
class BooleanWeight : public Weight {
public:
    BooleanWeight(const BooleanQuery& query, IndexSearcher& searcher, ScoreMode scoreMode,
                  float boost)
        : query_(query)
        , searcher_(searcher)
        , scoreMode_(scoreMode)
        , boost_(boost) {
        // Create weights for all sub-queries
        for (const auto& clause : query_.clauses()) {
            auto weight = clause.query->createWeight(searcher, scoreMode, boost);
            weights_.push_back({std::move(weight), clause.occur});
        }
    }

    std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const override {
        // Create scorers for each clause
        std::vector<std::unique_ptr<Scorer>> mustScorers;
        std::vector<std::unique_ptr<Scorer>> shouldScorers;
        std::vector<std::unique_ptr<Scorer>> filterScorers;
        std::vector<std::unique_ptr<Scorer>> mustNotScorers;

        for (const auto& weightClause : weights_) {
            auto scorer = weightClause.weight->scorer(context);
            if (!scorer) {
                // If a MUST or FILTER clause has no matches, entire query has no matches
                if (weightClause.occur == Occur::MUST || weightClause.occur == Occur::FILTER) {
                    return nullptr;
                }
                continue;
            }

            switch (weightClause.occur) {
                case Occur::MUST:
                    mustScorers.push_back(std::move(scorer));
                    break;
                case Occur::SHOULD:
                    shouldScorers.push_back(std::move(scorer));
                    break;
                case Occur::FILTER:
                    filterScorers.push_back(std::move(scorer));
                    break;
                case Occur::MUST_NOT:
                    mustNotScorers.push_back(std::move(scorer));
                    break;
            }
        }

        // Build required scorer (MUST + FILTER)
        std::unique_ptr<Scorer> reqScorer;

        if (!mustScorers.empty() && !filterScorers.empty()) {
            // Combine MUST and FILTER with conjunction
            std::vector<std::unique_ptr<Scorer>> combined;
            for (auto& s : mustScorers) {
                combined.push_back(std::move(s));
            }
            for (auto& s : filterScorers) {
                combined.push_back(std::move(s));
            }
            reqScorer = std::make_unique<ConjunctionScorer>(*this, std::move(combined));
        } else if (!mustScorers.empty()) {
            if (mustScorers.size() == 1) {
                reqScorer = std::move(mustScorers[0]);
            } else {
                reqScorer = std::make_unique<ConjunctionScorer>(*this, std::move(mustScorers));
            }
        } else if (!filterScorers.empty()) {
            if (filterScorers.size() == 1) {
                reqScorer = std::move(filterScorers[0]);
            } else {
                reqScorer = std::make_unique<ConjunctionScorer>(*this, std::move(filterScorers));
            }
        }

        // Add SHOULD clauses
        if (!shouldScorers.empty()) {
            if (reqScorer) {
                // Combine required and optional
                std::vector<std::unique_ptr<Scorer>> combined;
                combined.push_back(std::move(reqScorer));
                for (auto& s : shouldScorers) {
                    combined.push_back(std::move(s));
                }
                reqScorer = std::make_unique<DisjunctionScorer>(
                    *this, std::move(combined), 1 + query_.getMinimumNumberShouldMatch());
            } else {
                // Pure disjunction - check if we should use WAND
                const auto& config = searcher_.getConfig();
                int minShouldMatch = query_.getMinimumNumberShouldMatch();

                // WAND is beneficial when:
                // 1. ScoreMode requires scoring (COMPLETE or TOP_SCORES)
                //    - TOP_SCORES is the primary WAND mode: enables early termination
                //      via setMinCompetitiveScore() from TopScoreDocCollector
                //    - COMPLETE also benefits from block-max skipping
                // 2. We have 2+ terms (WAND needs multiple posting lists)
                //
                // Note: Even 2-term queries benefit from WAND when minCompetitiveScore
                // is set (TOP_SCORES mode), because WAND can skip entire blocks where
                // the block-max score cannot beat the threshold.
                bool useWAND = config.enable_block_max_wand &&
                               scoreMode_ != ScoreMode::COMPLETE_NO_SCORES &&
                               shouldScorers.size() >= 2;

                if (useWAND) {
                    // Use Block-Max WAND for early termination
                    BM25Similarity similarity;  // Default parameters
                    reqScorer = std::make_unique<WANDScorer>(
                        shouldScorers, similarity, minShouldMatch);
                } else {
                    // Use standard exhaustive disjunction
                    // This is optimal for simple queries (< 3 terms with minShouldMatch=0)
                    reqScorer = std::make_unique<DisjunctionScorer>(
                        *this, std::move(shouldScorers), minShouldMatch);
                }
            }
        }

        if (!reqScorer) {
            // No required or optional clauses - no matches
            return nullptr;
        }

        // Apply MUST_NOT exclusions
        if (!mustNotScorers.empty()) {
            for (auto& exclScorer : mustNotScorers) {
                reqScorer = std::make_unique<ReqExclScorer>(std::move(reqScorer),
                                                             std::move(exclScorer));
            }
        }

        return reqScorer;
    }

    std::unique_ptr<BulkScorer> bulkScorer(const index::LeafReaderContext& context) const override {
        const auto& config = searcher_.getConfig();

        // MaxScoreBulkScorer is only used for pure disjunctions with WAND enabled
        // and scoring requested (TOP_SCORES or COMPLETE)
        if (!config.enable_block_max_wand) return nullptr;
        if (scoreMode_ == ScoreMode::COMPLETE_NO_SCORES) return nullptr;
        if (!query_.isPureDisjunction()) return nullptr;

        int minShouldMatch = query_.getMinimumNumberShouldMatch();
        if (minShouldMatch > 1) return nullptr;  // Not supported yet

        // Create scorers for SHOULD clauses
        std::vector<std::unique_ptr<Scorer>> scorers;
        for (const auto& wc : weights_) {
            auto scorer = wc.weight->scorer(context);
            if (scorer) {
                scorers.push_back(std::move(scorer));
            }
        }

        if (scorers.size() < 2) return nullptr;  // Need 2+ for window-based scoring

        int maxDoc = context.reader ? context.reader->maxDoc() : 0;
        return std::make_unique<MaxScoreBulkScorer>(maxDoc, std::move(scorers));
    }

    const Query& getQuery() const override { return query_; }

    std::string toString() const override {
        std::ostringstream oss;
        oss << "weight(" << query_.toString("") << ")";
        return oss.str();
    }

private:
    struct WeightClause {
        std::unique_ptr<Weight> weight;
        Occur occur;
    };

    const BooleanQuery& query_;
    IndexSearcher& searcher_;
    ScoreMode scoreMode_;
    float boost_;
    std::vector<WeightClause> weights_;
};

// ==================== BooleanQuery::Builder ====================

BooleanQuery::Builder::Builder()
    : minimumNumberShouldMatch_(0) {}

BooleanQuery::Builder& BooleanQuery::Builder::add(std::shared_ptr<Query> query, Occur occur) {
    clauses_.emplace_back(std::move(query), occur);
    return *this;
}

BooleanQuery::Builder& BooleanQuery::Builder::add(const BooleanClause& clause) {
    clauses_.push_back(clause);
    return *this;
}

BooleanQuery::Builder& BooleanQuery::Builder::setMinimumNumberShouldMatch(int min) {
    minimumNumberShouldMatch_ = min;
    return *this;
}

std::unique_ptr<BooleanQuery> BooleanQuery::Builder::build() {
    return std::unique_ptr<BooleanQuery>(
        new BooleanQuery(std::move(clauses_), minimumNumberShouldMatch_));
}

// ==================== BooleanQuery ====================

BooleanQuery::BooleanQuery(std::vector<BooleanClause> clauses, int minimumNumberShouldMatch)
    : clauses_(std::move(clauses))
    , minimumNumberShouldMatch_(minimumNumberShouldMatch) {}

bool BooleanQuery::isPureDisjunction() const {
    for (const auto& clause : clauses_) {
        if (clause.occur != Occur::SHOULD) {
            return false;
        }
    }
    return !clauses_.empty();
}

bool BooleanQuery::isRequired() const {
    for (const auto& clause : clauses_) {
        if (clause.occur == Occur::MUST || clause.occur == Occur::FILTER) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<Weight> BooleanQuery::createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                                    float boost) const {
    return std::make_unique<BooleanWeight>(*this, searcher, scoreMode, boost);
}

std::unique_ptr<Query> BooleanQuery::rewrite(index::IndexReader& reader) const {
    // Rewrite all sub-queries
    std::vector<BooleanClause> rewritten;
    bool changed = false;

    for (const auto& clause : clauses_) {
        auto rewrittenQuery = clause.query->rewrite(reader);
        if (rewrittenQuery.get() != clause.query.get()) {
            changed = true;
        }
        rewritten.emplace_back(std::shared_ptr<Query>(rewrittenQuery.release()), clause.occur);
    }

    if (!changed) {
        return clone();
    }

    return std::unique_ptr<BooleanQuery>(
        new BooleanQuery(std::move(rewritten), minimumNumberShouldMatch_));
}

std::string BooleanQuery::toString(const std::string& field) const {
    std::ostringstream oss;

    for (size_t i = 0; i < clauses_.size(); ++i) {
        if (i > 0) {
            oss << " ";
        }

        const auto& clause = clauses_[i];

        switch (clause.occur) {
            case Occur::MUST:
                oss << "+";
                break;
            case Occur::MUST_NOT:
                oss << "-";
                break;
            case Occur::SHOULD:
                // No prefix for SHOULD
                break;
            case Occur::FILTER:
                oss << "#";
                break;
        }

        oss << clause.query->toString(field);
    }

    if (minimumNumberShouldMatch_ > 0) {
        oss << "~" << minimumNumberShouldMatch_;
    }

    return oss.str();
}

bool BooleanQuery::equals(const Query& other) const {
    if (auto* bq = dynamic_cast<const BooleanQuery*>(&other)) {
        if (clauses_.size() != bq->clauses_.size()) {
            return false;
        }
        if (minimumNumberShouldMatch_ != bq->minimumNumberShouldMatch_) {
            return false;
        }
        for (size_t i = 0; i < clauses_.size(); ++i) {
            if (clauses_[i].occur != bq->clauses_[i].occur) {
                return false;
            }
            if (!clauses_[i].query->equals(*bq->clauses_[i].query)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

size_t BooleanQuery::hashCode() const {
    size_t h = 0;
    for (const auto& clause : clauses_) {
        h ^= clause.query->hashCode() + static_cast<size_t>(clause.occur);
        h = (h << 1) | (h >> 63);  // Rotate
    }
    h ^= minimumNumberShouldMatch_;
    return h;
}

std::unique_ptr<Query> BooleanQuery::clone() const {
    std::vector<BooleanClause> clonedClauses;
    for (const auto& clause : clauses_) {
        clonedClauses.emplace_back(std::shared_ptr<Query>(clause.query->clone().release()),
                                    clause.occur);
    }
    return std::unique_ptr<BooleanQuery>(
        new BooleanQuery(std::move(clonedClauses), minimumNumberShouldMatch_));
}

}  // namespace search
}  // namespace diagon
