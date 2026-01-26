// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/TermQuery.h"

#include "diagon/index/DocValues.h"
#include "diagon/index/IndexReader.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/Scorer.h"

#include <sstream>

namespace diagon {
namespace search {

// ==================== TermScorer ====================

/**
 * TermScorer - Scorer for TermQuery
 *
 * Iterates postings and computes BM25 scores
 */
class TermScorer : public Scorer {
public:
    TermScorer(const Weight& weight, std::unique_ptr<index::PostingsEnum> postings,
               const BM25Similarity::SimScorer& simScorer,
               index::NumericDocValues* norms)
        : weight_(weight)
        , postings_(std::move(postings))
        , simScorer_(simScorer)
        , norms_(norms)
        , doc_(-1)
        , freq_(0) {}

    // ==================== DocIdSetIterator ====================

    int docID() const override { return doc_; }

    int nextDoc() override {
        doc_ = postings_->nextDoc();
        if (doc_ != NO_MORE_DOCS) {
            freq_ = postings_->freq();
        }
        return doc_;
    }

    int advance(int target) override {
        doc_ = postings_->advance(target);
        if (doc_ != NO_MORE_DOCS) {
            freq_ = postings_->freq();
        }
        return doc_;
    }

    int64_t cost() const override {
        // Cost is approximately the document frequency
        // TODO: Get actual docFreq from terms
        return 1000;  // Placeholder
    }

    // ==================== Scorer ====================

    float score() const override {
        // BM25 score using term frequency and document norms
        long norm = 1L;  // Default norm if not available
        if (norms_ && norms_->advanceExact(doc_)) {
            norm = norms_->longValue();
        }
        return simScorer_.score(static_cast<float>(freq_), norm);
    }

    const Weight& getWeight() const override { return weight_; }

private:
    const Weight& weight_;
    std::unique_ptr<index::PostingsEnum> postings_;
    BM25Similarity::SimScorer simScorer_;
    index::NumericDocValues* norms_;  // Non-owning pointer to norms
    int doc_;
    int freq_;
};

// ==================== TermWeight ====================

/**
 * TermWeight - Weight for TermQuery
 *
 * Creates TermScorer for each segment
 */
class TermWeight : public Weight {
public:
    TermWeight(const TermQuery& query, IndexSearcher& searcher, ScoreMode scoreMode, float boost)
        : query_(query)
        , searcher_(searcher)
        , scoreMode_(scoreMode)
        , boost_(boost)
        , simScorer_(createScorer(query, searcher, boost)) {}

private:
    static BM25Similarity::SimScorer createScorer(const TermQuery& query, IndexSearcher& searcher,
                                                  float boost) {
        // Get collection statistics
        // Phase 4: Simplified - use reader's maxDoc
        int64_t docCount = searcher.getIndexReader().maxDoc();

        // TODO Phase 5: Get actual statistics from IndexSearcher
        CollectionStatistics collectionStats(
            query.getTerm().field(),
            docCount,       // maxDoc
            docCount,       // docCount (all docs have field - simplified)
            docCount * 10,  // sumTotalTermFreq (estimated)
            docCount        // sumDocFreq (estimated)
        );

        // TODO Phase 5: Get actual term statistics
        TermStatistics termStats(query.getTerm().bytes(),
                                 docCount / 10,  // docFreq (estimated - 10% of docs)
                                 docCount        // totalTermFreq (estimated)
        );

        // Create similarity scorer
        BM25Similarity similarity;
        return similarity.scorer(boost, collectionStats, termStats);
    }

public:
    std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const override {
        // Get terms for field
        auto terms = context.reader->terms(query_.getTerm().field());
        if (!terms) {
            return nullptr;  // Field doesn't exist in this segment
        }

        // Get terms enum
        auto termsEnum = terms->iterator();
        if (!termsEnum) {
            return nullptr;
        }

        // Seek to term
        if (!termsEnum->seekExact(query_.getTerm().bytes())) {
            return nullptr;  // Term doesn't exist in this segment
        }

        // Get postings
        auto postings = termsEnum->postings();
        if (!postings) {
            return nullptr;
        }

        // Get norms for BM25 length normalization
        auto* norms = context.reader->getNormValues(query_.getTerm().field());

        // Create scorer (takes ownership of postings)
        return std::make_unique<TermScorer>(*this, std::move(postings), simScorer_, norms);
    }

    const Query& getQuery() const override { return query_; }

    std::string toString() const override {
        std::ostringstream oss;
        oss << "weight(" << query_.toString(query_.getTerm().field()) << ")";
        return oss.str();
    }

private:
    const TermQuery& query_;
    IndexSearcher& searcher_;
    ScoreMode scoreMode_;
    float boost_;
    BM25Similarity::SimScorer simScorer_;
};

// ==================== TermQuery ====================

TermQuery::TermQuery(const Term& term)
    : term_(term) {}

std::unique_ptr<Weight> TermQuery::createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                                float boost) const {
    return std::make_unique<TermWeight>(*this, searcher, scoreMode, boost);
}

std::string TermQuery::toString(const std::string& field) const {
    std::ostringstream oss;
    if (term_.field() != field) {
        oss << term_.field() << ":";
    }
    oss << term_.text();
    return oss.str();
}

bool TermQuery::equals(const Query& other) const {
    if (auto* tq = dynamic_cast<const TermQuery*>(&other)) {
        return term_.equals(tq->term_);
    }
    return false;
}

size_t TermQuery::hashCode() const {
    return term_.hashCode();
}

std::unique_ptr<Query> TermQuery::clone() const {
    return std::make_unique<TermQuery>(term_);
}

}  // namespace search
}  // namespace diagon
