// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/TermQuery.h"

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
        , freq_(0)
        , impactsEnum_(dynamic_cast<codecs::lucene104::Lucene104PostingsEnumWithImpacts*>(postings_.get()))
        , normsSize_(0)
        , normsData_(norms ? norms->normsData(&normsSize_) : nullptr) {}

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
        return postings_->cost();
    }

    int advanceShallow(int target) override {
        if (impactsEnum_) {
            return impactsEnum_->advanceShallow(target);
        }
        return NO_MORE_DOCS;
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

    // Block-Max WAND support: compute maximum possible score
    float getMaxScore(int upTo) const override {
        if (impactsEnum_) {
            // Get max frequency and max norm in single pass over skip entries
            int maxFreq, maxNorm;
            impactsEnum_->getMaxFreqAndNorm(upTo, maxFreq, maxNorm);

            // Compute BM25 upper bound with these maximums
            return simScorer_.score(static_cast<float>(maxFreq), maxNorm);
        } else {
            // Fallback: use conservative global maximum
            constexpr float MAX_FREQ = 10000.0f;
            constexpr long SHORTEST_DOC_NORM = 127;  // Length 1.0
            return simScorer_.score(MAX_FREQ, SHORTEST_DOC_NORM);
        }
    }

    // Phase 2: Smart upTo calculation - get next block boundary
    int getNextBlockBoundary(int target) const override {
        // Delegate to PostingsEnum (which knows about skip entries)
        return postings_->getNextBlockBoundary(target);
    }

    // ==================== Batch Scoring ====================

    int scoreBatch(int upTo, int* outDocs, float* outScores, int maxCount) override {
        if (!impactsEnum_) {
            return Scorer::scoreBatch(upTo, outDocs, outScores, maxCount);
        }

        int freqsBuf[SCORER_BATCH_SIZE];
        int batchSize = std::min(maxCount, static_cast<int>(SCORER_BATCH_SIZE));

        // Phase 1: Batch decode docs+freqs (non-virtual, direct buffer drain)
        int count = impactsEnum_->drainBatch(upTo, outDocs, freqsBuf, batchSize);

        if (count == 0) {
            doc_ = impactsEnum_->docID();
            return 0;
        }

        // Phase 2: Batch norms lookup + BM25 scoring
        if (normsData_) {
            // Fast path: direct array access (no virtual dispatch)
            for (int i = 0; i < count; i++) {
                long norm = (outDocs[i] < normsSize_)
                    ? static_cast<long>(normsData_[outDocs[i]]) : 1L;
                outScores[i] = simScorer_.score(static_cast<float>(freqsBuf[i]), norm);
            }
        } else {
            // Fallback: virtual dispatch per doc
            for (int i = 0; i < count; i++) {
                long norm = 1L;
                if (norms_ && norms_->advanceExact(outDocs[i])) {
                    norm = norms_->longValue();
                }
                outScores[i] = simScorer_.score(static_cast<float>(freqsBuf[i]), norm);
            }
        }

        // Update TermScorer state to match impacts enum
        doc_ = impactsEnum_->docID();
        freq_ = freqsBuf[count - 1];

        return count;
    }

private:
    const Weight& weight_;
    std::unique_ptr<index::PostingsEnum> postings_;
    BM25Similarity::SimScorer simScorer_;
    index::NumericDocValues* norms_;  // Non-owning pointer to norms
    int doc_;
    int freq_;
    codecs::lucene104::Lucene104PostingsEnumWithImpacts* impactsEnum_;  // Cached typed pointer (non-owning)
    int normsSize_;            // Size of normsData_ array (must be declared before normsData_ for init order)
    const int8_t* normsData_;  // Direct norms array pointer (from any NumericDocValues with normsData())
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
        // Get actual collection statistics from index
        const std::string& field = query.getTerm().field();
        auto& reader = searcher.getIndexReader();

        int64_t maxDoc = reader.maxDoc();
        int64_t sumTotalTermFreq = 0;
        int64_t sumDocFreq = 0;

        // Aggregate statistics across all segments
        for (const auto& ctx : reader.leaves()) {
            auto terms = ctx.reader->terms(field);
            if (!terms) continue;

            int64_t segmentSumTotalTermFreq = terms->getSumTotalTermFreq();
            int64_t segmentSumDocFreq = terms->getSumDocFreq();

            // Only accumulate if valid (not -1)
            if (segmentSumTotalTermFreq > 0) {
                sumTotalTermFreq += segmentSumTotalTermFreq;
            }
            if (segmentSumDocFreq > 0) {
                sumDocFreq += segmentSumDocFreq;
            }
        }

        // Fallback to estimates if statistics not available
        if (sumTotalTermFreq <= 0) {
            sumTotalTermFreq = maxDoc * 10;
        }
        if (sumDocFreq <= 0) {
            sumDocFreq = maxDoc;
        }

        // IMPORTANT: Use maxDoc for docCount (total documents in index)
        // This matches Lucene's CollectionStatistics behavior
        // Do NOT use terms.getDocCount() which only counts documents with terms
        int64_t docCount = maxDoc;

        CollectionStatistics collectionStats(
            field,
            maxDoc,
            docCount,
            sumTotalTermFreq,
            sumDocFreq
        );

        // Get actual term statistics by seeking to the term
        int64_t termDocFreq = 0;
        int64_t termTotalTermFreq = 0;

        for (const auto& ctx : reader.leaves()) {
            auto terms = ctx.reader->terms(field);
            if (!terms) continue;

            auto termsEnum = terms->iterator();
            if (!termsEnum) continue;

            if (termsEnum->seekExact(query.getTerm().bytes())) {
                termDocFreq += termsEnum->docFreq();
                int64_t ttf = termsEnum->totalTermFreq();
                if (ttf > 0) {
                    termTotalTermFreq += ttf;
                }
            }
        }

        // Fallback if term not found (shouldn't happen in normal search)
        if (termDocFreq == 0) {
            termDocFreq = maxDoc / 10;
            termTotalTermFreq = maxDoc;
        }

        TermStatistics termStats(query.getTerm().bytes(),
                                 termDocFreq,
                                 termTotalTermFreq);

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

        const auto& config = searcher_.getConfig();
        bool useWAND = config.enable_block_max_wand;

        // Get postings - try impacts-aware if WAND is enabled
        std::unique_ptr<index::PostingsEnum> postings;

        if (useWAND) {
            // Try to get impacts-aware postings for WAND optimization
            auto* segmentTermsEnum = dynamic_cast<codecs::blocktree::SegmentTermsEnum*>(termsEnum.get());
            if (segmentTermsEnum) {
                postings = segmentTermsEnum->impactsPostings();
            }
        }

        // Fallback to regular postings if impacts not available
        if (!postings) {
            postings = termsEnum->postings(false);
        }

        if (!postings) {
            return nullptr;
        }

        // Get norms for BM25 length normalization
        auto* norms = context.reader->getNormValues(query_.getTerm().field());

        // TermScorer has built-in batch capability via scoreBatch()
        return std::make_unique<TermScorer>(
            *this, std::move(postings), simScorer_, norms);
    }

    int count(const index::LeafReaderContext& context) const override {
        // Fast path: no deletions → docFreq() is exact (O(1))
        if (!context.reader->hasDeletions()) {
            auto terms = context.reader->terms(query_.getTerm().field());
            if (!terms) return 0;

            auto termsEnum = terms->iterator();
            if (!termsEnum) return 0;

            if (termsEnum->seekExact(query_.getTerm().bytes())) {
                return termsEnum->docFreq();
            }
            return 0;  // Term not in this segment
        }
        // With deletions: cannot count without iterating
        return -1;
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
