// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/PhraseQuery.h"

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/IndexReader.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/Scorer.h"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace diagon {
namespace search {

// ==================== PhraseScorer ====================

/**
 * PhraseScorer - Exact phrase matching scorer (slop=0).
 *
 * Algorithm (following Lucene's ExactPhraseMatcher):
 * 1. Conjunction: advance all PostingsEnums to the same doc
 * 2. Position check: for each matching doc, iterate positions of lead term
 *    (lowest docFreq). For each lead position, check if all other terms
 *    are at expected positions.
 * 3. Scoring: phraseFreq = count of phrase matches. Score = BM25(phraseFreq, norm).
 */
class PhraseScorer : public Scorer {
public:
    struct PostingsAndPosition {
        index::PostingsEnum* postings;
        int offset;  // Position offset from query
        int freq;    // Cached freq for current doc
        int pos;     // Current position
        int upTo;    // Positions consumed via nextPosition()
    };

    PhraseScorer(const Weight& weight, std::vector<std::unique_ptr<index::PostingsEnum>> postings,
                 const std::vector<int>& positions, const BM25Similarity::SimScorer& simScorer,
                 index::NumericDocValues* norms)
        : weight_(weight)
        , ownedPostings_(std::move(postings))
        , simScorer_(simScorer)
        , norms_(norms)
        , doc_(-1)
        , phraseFreq_(0.0f) {
        // Build PostingsAndPosition array sorted by docFreq ascending
        // (lead = term with fewest postings = most selective)
        for (size_t i = 0; i < ownedPostings_.size(); i++) {
            PostingsAndPosition pap;
            pap.postings = ownedPostings_[i].get();
            pap.offset = positions[i];
            pap.freq = 0;
            pap.pos = -1;
            pap.upTo = 0;
            postingsAndPositions_.push_back(pap);
        }

        // Sort by cost (docFreq) ascending - lead is lowest freq
        std::sort(postingsAndPositions_.begin(), postingsAndPositions_.end(),
                  [](const PostingsAndPosition& a, const PostingsAndPosition& b) {
                      return a.postings->cost() < b.postings->cost();
                  });
    }

    // ==================== DocIdSetIterator ====================

    int docID() const override { return doc_; }

    int nextDoc() override {
        while (true) {
            // Advance lead (lowest freq term)
            doc_ = postingsAndPositions_[0].postings->nextDoc();
            if (doc_ == NO_MORE_DOCS) {
                return NO_MORE_DOCS;
            }

            // Try to advance all others to the same doc (conjunction)
            if (advanceAll() && matchPositions()) {
                return doc_;
            }
        }
    }

    int advance(int target) override {
        doc_ = postingsAndPositions_[0].postings->advance(target);
        if (doc_ == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }

        while (true) {
            if (advanceAll() && matchPositions()) {
                return doc_;
            }
            doc_ = postingsAndPositions_[0].postings->nextDoc();
            if (doc_ == NO_MORE_DOCS) {
                return NO_MORE_DOCS;
            }
        }
    }

    int64_t cost() const override {
        // Cost is the cost of the lead (most selective term)
        return postingsAndPositions_.empty() ? 0 : postingsAndPositions_[0].postings->cost();
    }

    // ==================== Scorer ====================

    float score() const override {
        long norm = 1L;
        if (norms_ && norms_->advanceExact(doc_)) {
            norm = norms_->longValue();
        }
        return simScorer_.score(phraseFreq_, norm);
    }

    const Weight& getWeight() const override { return weight_; }

    float getMaxScore(int /*upTo*/) const override {
        // Conservative: no early termination for phrase queries
        constexpr float MAX_FREQ = 10000.0f;
        constexpr long SHORTEST_DOC_NORM = 127;
        return simScorer_.score(MAX_FREQ, SHORTEST_DOC_NORM);
    }

private:
    const Weight& weight_;
    std::vector<std::unique_ptr<index::PostingsEnum>> ownedPostings_;
    std::vector<PostingsAndPosition> postingsAndPositions_;
    BM25Similarity::SimScorer simScorer_;
    index::NumericDocValues* norms_;
    int doc_;
    float phraseFreq_;

    /**
     * Advance all non-lead PostingsEnums to the current doc.
     * If any advances past doc_, re-advance lead. Returns true when
     * all enums are on the same doc.
     */
    bool advanceAll() {
        for (size_t i = 1; i < postingsAndPositions_.size(); i++) {
            auto* pe = postingsAndPositions_[i].postings;
            int d = pe->docID();
            if (d < doc_) {
                d = pe->advance(doc_);
            }
            if (d > doc_) {
                // This term jumped past our doc - need to re-advance lead
                doc_ = postingsAndPositions_[0].postings->advance(d);
                if (doc_ == NO_MORE_DOCS) {
                    return false;
                }
                // Restart conjunction from the beginning
                return advanceAll();
            }
        }
        return true;
    }

    /**
     * Check if all terms appear at the expected positions in the current doc.
     * Sets phraseFreq_ to the count of phrase matches.
     */
    bool matchPositions() {
        phraseFreq_ = 0.0f;

        // Initialize position state for all PostingsEnums
        for (auto& pap : postingsAndPositions_) {
            pap.freq = pap.postings->freq();
            pap.pos = -1;
            pap.upTo = 0;
        }

        // Lead term drives iteration
        auto& lead = postingsAndPositions_[0];
        while (lead.upTo < lead.freq) {
            lead.pos = lead.postings->nextPosition();
            lead.upTo++;

            if (lead.pos < 0)
                break;

            // Expected phrase position based on lead
            int phrasePos = lead.pos - lead.offset;

            bool allMatch = true;
            for (size_t j = 1; j < postingsAndPositions_.size(); j++) {
                auto& other = postingsAndPositions_[j];
                int expectedPos = phrasePos + other.offset;

                // Advance other's position to expectedPos
                while (other.pos < expectedPos && other.upTo < other.freq) {
                    other.pos = other.postings->nextPosition();
                    other.upTo++;
                }

                if (other.pos != expectedPos) {
                    allMatch = false;
                    break;
                }
            }

            if (allMatch) {
                phraseFreq_ += 1.0f;
            }
        }

        return phraseFreq_ > 0;
    }
};

// ==================== PhraseWeight ====================

class PhraseWeight : public Weight {
public:
    PhraseWeight(const PhraseQuery& query, IndexSearcher& searcher,
                 [[maybe_unused]] ScoreMode scoreMode, float boost)
        : query_(query)
        , searcher_(searcher)
        , simScorer_(createScorer(query, searcher, boost)) {}

    std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const override {
        auto terms = context.reader->terms(query_.getField());
        if (!terms) {
            return nullptr;
        }

        const auto& queryTerms = query_.getTerms();
        const auto& positions = query_.getPositions();

        // Collect PostingsEnums with positions for all terms
        std::vector<std::unique_ptr<index::PostingsEnum>> postingsEnums;
        postingsEnums.reserve(queryTerms.size());

        for (const auto& qt : queryTerms) {
            auto termsEnum = terms->iterator();
            if (!termsEnum || !termsEnum->seekExact(qt.bytes())) {
                return nullptr;  // Term not found in this segment
            }

            // Request positions via features flag
            auto pe = termsEnum->postings(index::FEATURE_POSITIONS);
            if (!pe) {
                return nullptr;
            }
            postingsEnums.push_back(std::move(pe));
        }

        auto* norms = context.reader->getNormValues(query_.getField());

        return std::make_unique<PhraseScorer>(*this, std::move(postingsEnums), positions,
                                              simScorer_, norms);
    }

    const Query& getQuery() const override { return query_; }

    std::string toString() const override {
        return "weight(" + query_.toString(query_.getField()) + ")";
    }

private:
    const PhraseQuery& query_;
    IndexSearcher& searcher_;
    BM25Similarity::SimScorer simScorer_;

    static BM25Similarity::SimScorer createScorer(const PhraseQuery& query, IndexSearcher& searcher,
                                                  float boost) {
        const std::string& field = query.getField();
        auto& reader = searcher.getIndexReader();

        int64_t maxDoc = reader.maxDoc();
        int64_t sumTotalTermFreq = 0;
        int64_t sumDocFreq = 0;

        for (const auto& ctx : reader.leaves()) {
            auto terms = ctx.reader->terms(field);
            if (!terms)
                continue;

            int64_t sttf = terms->getSumTotalTermFreq();
            int64_t sdf = terms->getSumDocFreq();
            if (sttf > 0)
                sumTotalTermFreq += sttf;
            if (sdf > 0)
                sumDocFreq += sdf;
        }

        if (sumTotalTermFreq <= 0)
            sumTotalTermFreq = maxDoc * 10;
        if (sumDocFreq <= 0)
            sumDocFreq = maxDoc;

        int64_t docCount = maxDoc;
        CollectionStatistics collectionStats(field, maxDoc, docCount, sumTotalTermFreq, sumDocFreq);

        // Aggregate term statistics across all phrase terms (sum of IDFs)
        // Use the rarest term for IDF (most discriminative)
        int64_t minDocFreq = maxDoc;
        int64_t totalTermFreqSum = 0;

        for (const auto& qt : query.getTerms()) {
            int64_t termDocFreq = 0;
            int64_t termTotalTermFreq = 0;
            for (const auto& ctx : reader.leaves()) {
                auto terms = ctx.reader->terms(field);
                if (!terms)
                    continue;
                auto te = terms->iterator();
                if (!te)
                    continue;
                if (te->seekExact(qt.bytes())) {
                    termDocFreq += te->docFreq();
                    int64_t ttf = te->totalTermFreq();
                    if (ttf > 0)
                        termTotalTermFreq += ttf;
                }
            }
            if (termDocFreq > 0) {
                minDocFreq = std::min(minDocFreq, termDocFreq);
                totalTermFreqSum += termTotalTermFreq;
            }
        }

        if (minDocFreq <= 0)
            minDocFreq = 1;
        if (totalTermFreqSum <= 0)
            totalTermFreqSum = minDocFreq;

        // Use the rarest term's docFreq for IDF - phrase can't match more docs
        // than the rarest term
        TermStatistics termStats(query.getTerms()[0].bytes(), minDocFreq, totalTermFreqSum);

        BM25Similarity similarity;
        return similarity.scorer(boost, collectionStats, termStats);
    }
};

// ==================== PhraseQuery ====================

PhraseQuery::PhraseQuery(const std::string& field, std::vector<Term> terms,
                         std::vector<int> positions, int slop)
    : field_(field)
    , terms_(std::move(terms))
    , positions_(std::move(positions))
    , slop_(slop) {
    if (terms_.size() != positions_.size()) {
        throw std::invalid_argument("terms and positions must have same size");
    }
}

std::unique_ptr<Weight> PhraseQuery::createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                                  float boost) const {
    return std::make_unique<PhraseWeight>(*this, searcher, scoreMode, boost);
}

std::unique_ptr<Query> PhraseQuery::rewrite(index::IndexReader& reader) const {
    // Single-term phrase rewrites to TermQuery
    if (terms_.size() == 1) {
        return std::make_unique<TermQuery>(terms_[0]);
    }
    // Empty phrase matches nothing
    if (terms_.empty()) {
        return clone();
    }
    return clone();
}

std::string PhraseQuery::toString(const std::string& field) const {
    std::ostringstream oss;
    if (field_ != field) {
        oss << field_ << ":";
    }
    oss << "\"";
    for (size_t i = 0; i < terms_.size(); i++) {
        if (i > 0)
            oss << " ";
        // Output UTF-8 string directly from bytes, not hex-encoded
        const auto& b = terms_[i].bytes();
        oss << std::string(reinterpret_cast<const char*>(b.data()), b.length());
    }
    oss << "\"";
    if (slop_ > 0) {
        oss << "~" << slop_;
    }
    return oss.str();
}

bool PhraseQuery::equals(const Query& other) const {
    auto* pq = dynamic_cast<const PhraseQuery*>(&other);
    if (!pq)
        return false;
    if (field_ != pq->field_ || slop_ != pq->slop_)
        return false;
    if (terms_.size() != pq->terms_.size())
        return false;
    for (size_t i = 0; i < terms_.size(); i++) {
        if (!terms_[i].equals(pq->terms_[i]))
            return false;
        if (positions_[i] != pq->positions_[i])
            return false;
    }
    return true;
}

size_t PhraseQuery::hashCode() const {
    size_t h = std::hash<std::string>{}(field_);
    for (const auto& t : terms_) {
        h ^= t.hashCode() + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    h ^= std::hash<int>{}(slop_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

std::unique_ptr<Query> PhraseQuery::clone() const {
    auto terms = terms_;
    auto positions = positions_;
    return std::make_unique<PhraseQuery>(field_, std::move(terms), std::move(positions), slop_);
}

}  // namespace search
}  // namespace diagon
