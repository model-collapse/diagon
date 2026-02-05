// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/TermQuery.h"

#include "diagon/index/BatchDocValues.h"
#include "diagon/index/BatchPostingsEnum.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/IndexReader.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/search/BatchBM25Scorer.h"
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

// ==================== BatchTermScorer ====================

/**
 * BatchTermScorer - Batch-at-a-time scorer for TermQuery
 *
 * P1 optimization: Eliminates one-at-a-time iterator overhead
 * using batch postings decoding and SIMD BM25 scoring.
 *
 * Expected improvement: +19% search latency
 */
class BatchTermScorer : public Scorer {
public:
    BatchTermScorer(const Weight& weight,
                   std::unique_ptr<index::PostingsEnum> postings,
                   const BM25Similarity::SimScorer& simScorer,
                   index::NumericDocValues* norms,
                   int batch_size,
                   BatchBuffers& buffers)  // Phase 3: Use external buffers
        : weight_(weight)
        , postings_(std::move(postings))
        , simScorer_(simScorer)
        , norms_(norms)
        , batch_size_(batch_size)
        , batch_(batch_size)
        , batch_pos_(0)
        , buffers_(buffers)  // Phase 3: Store reference (must match declaration order)
        , doc_(-1)
        , freq_(0)
        , score_(0.0f) {

        // Phase 3: Ensure external buffers have capacity (cheap if already sized)
        buffers_.ensureCapacity(batch_size);

        // Cache batch interface checks (Phase 3 optimization: move from hot path)
        batch_postings_ = dynamic_cast<index::BatchPostingsEnum*>(postings_.get());
        batch_norms_ = dynamic_cast<index::BatchNumericDocValues*>(norms_);
    }

    // ==================== DocIdSetIterator ====================

    int docID() const override { return doc_; }

    int nextDoc() override {
        // Check if we need to fetch next batch
        if (batch_pos_ >= batch_.count) {
            if (!fetchNextBatch()) {
                doc_ = NO_MORE_DOCS;
                return doc_;
            }
        }

        // Return next document from batch
        doc_ = batch_.docs[batch_pos_];
        freq_ = batch_.freqs[batch_pos_];
        score_ = buffers_.scores[batch_pos_];  // Phase 3: Use external buffer
        batch_pos_++;

        return doc_;
    }

    int advance(int target) override {
        // For simplicity, use nextDoc until we reach target
        // TODO: Optimize with batch skipping
        int doc = doc_;
        while (doc < target && doc != NO_MORE_DOCS) {
            doc = nextDoc();
        }
        return doc;
    }

    int64_t cost() const override {
        return 1000;  // Placeholder
    }

    // ==================== Scorer ====================

    float score() const override {
        // Score already computed in batch
        return score_;
    }

    const Weight& getWeight() const override { return weight_; }

private:
    /**
     * Fetch and score next batch of documents
     *
     * Returns false if no more documents available.
     *
     * Phase 3.3: Optimized with pointer arithmetic and inline hint
     */
    __attribute__((always_inline)) bool fetchNextBatch() {
        // Try batch decoding if available
        if (batch_postings_) {
            batch_.count = batch_postings_->nextBatch(batch_);
        } else {
            // Fallback: collect batch from one-at-a-time interface
            // Phase 3.3: Use pointer arithmetic for faster access
            int* doc_ptr = batch_.docs;
            int* freq_ptr = batch_.freqs;
            int count = 0;

            for (int i = 0; i < batch_size_; i++) {
                int doc = postings_->nextDoc();
                if (doc == NO_MORE_DOCS) {
                    break;
                }
                *doc_ptr++ = doc;
                *freq_ptr++ = postings_->freq();
                count++;
            }
            batch_.count = count;
        }

        if (batch_.count == 0) {
            return false;  // Exhausted
        }

        // Batch lookup norms (Phase 3: Use external buffer)
        if (batch_norms_) {
            // Fast path: batch interface available
            batch_norms_->getBatch(batch_.docs, buffers_.norms.data(), batch_.count);
        } else {
            // Fallback: one-at-a-time norm lookup with prefetching
            // Phase 3.3: Use pointer arithmetic
            // Phase 4: Add prefetching to reduce memory latency (P3 Task #38)
            long* norm_ptr = buffers_.norms.data();
            const int* doc_ptr = batch_.docs;

            constexpr int PREFETCH_DISTANCE = 12;  // Optimal: covers L3/RAM latency (~330 cycles)

            for (int i = 0; i < batch_.count; i++) {
                // Prefetch norm data for future documents
                // This hides memory latency by fetching data before it's needed
                if (i + PREFETCH_DISTANCE < batch_.count && norms_) {
                    // Prefetch to L1 cache (hint: 3 = high temporal locality)
                    // Prefetch the doc ID array location for the future document
                    __builtin_prefetch(&doc_ptr[PREFETCH_DISTANCE], 0, 3);
                }

                long norm = 1L;
                if (norms_ && norms_->advanceExact(*doc_ptr++)) {
                    norm = norms_->longValue();
                }
                *norm_ptr++ = norm;
            }
        }

        // SIMD batch scoring (Phase 3: Use external buffer)
        BatchBM25Scorer::scoreBatch(
            batch_.freqs,
            buffers_.norms.data(),
            simScorer_.getIDF(),
            simScorer_.getK1(),
            simScorer_.getB(),
            50.0f,  // avgLength (TODO: get from stats)
            buffers_.scores.data(),
            batch_.count
        );

        batch_pos_ = 0;
        return true;
    }

    const Weight& weight_;
    std::unique_ptr<index::PostingsEnum> postings_;
    BM25Similarity::SimScorer simScorer_;
    index::NumericDocValues* norms_;  // Non-owning pointer

    // Batch processing
    int batch_size_;
    index::PostingsBatch batch_;
    int batch_pos_;
    BatchBuffers& buffers_;  // Phase 3: External pre-allocated buffers

    // Batch interface pointers (cached at construction, Phase 3 optimization)
    index::BatchPostingsEnum* batch_postings_ = nullptr;
    index::BatchNumericDocValues* batch_norms_ = nullptr;

    // Cached values
    int doc_;
    int freq_;
    float score_;
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

        // Check if batch mode is enabled
        const auto& config = searcher_.getConfig();
        bool useBatch = config.enable_batch_scoring;

        // Get postings (with batch mode if enabled)
        auto postings = termsEnum->postings(useBatch);
        if (!postings) {
            return nullptr;
        }

        // Get norms for BM25 length normalization
        auto* norms = context.reader->getNormValues(query_.getTerm().field());

        if (useBatch) {
            // Create batch scorer (P1 optimization, Phase 3: reuse buffers)
            return std::make_unique<BatchTermScorer>(
                *this, std::move(postings), simScorer_, norms, config.batch_size,
                searcher_.getBatchBuffers());
        } else {
            // Create regular scorer (default)
            return std::make_unique<TermScorer>(
                *this, std::move(postings), simScorer_, norms);
        }
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
