// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/TopScoreDocCollector.h"

#include "diagon/util/SearchProfiler.h"

#include <algorithm>
#include <cmath>

// SIMD headers for batched collection
#if defined(__AVX512F__) || defined(__AVX2__)
#    include <immintrin.h>
#endif

namespace diagon {
namespace search {

// ==================== Factory Methods ====================

std::unique_ptr<TopScoreDocCollector> TopScoreDocCollector::create(int numHits) {
    return create(numHits, 1000);
}

std::unique_ptr<TopScoreDocCollector> TopScoreDocCollector::create(int numHits,
                                                                   int totalHitsThreshold) {
    int effectiveThreshold = std::max(totalHitsThreshold, numHits);
    return std::unique_ptr<TopScoreDocCollector>(
        new TopScoreDocCollector(numHits, nullptr, effectiveThreshold));
}

std::unique_ptr<TopScoreDocCollector> TopScoreDocCollector::create(int numHits,
                                                                   const ScoreDoc& after) {
    return std::unique_ptr<TopScoreDocCollector>(new TopScoreDocCollector(numHits, &after, 1000));
}

// ==================== Constructor ====================

TopScoreDocCollector::TopScoreDocCollector(int numHits, const ScoreDoc* after,
                                           int totalHitsThreshold)
    : numHits_(numHits)
    , after_(after ? *after : ScoreDoc())
    , hasAfter_(after != nullptr)
    , totalHits_(0)
    , totalHitsRelation_(TotalHits::Relation::EQUAL_TO)
    , totalHitsThreshold_(totalHitsThreshold)
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
#if defined(__AVX512F__) || defined(__AVX2__)
    : batchPos_(0)
    , parent_(parent)
#else
    : parent_(parent)
#endif
    , docBase_(context.docBase)
    , scorer_(nullptr)
    , after_(parent->hasAfter_ ? &parent->after_ : nullptr)
    , segmentHitsFromCollect_(0)
    , scorerTracksTotalMatches_(false) {
}

TopScoreDocCollector::TopScoreLeafCollector::~TopScoreLeafCollector() {
#if defined(__AVX512F__) || defined(__AVX2__)
    // Flush any remaining documents in batch
    flushBatch();
#endif

    // Note: Do NOT access scorer_ here - it may be out of scope!
    // Total matches are captured in finishSegment() instead.
}

void TopScoreDocCollector::TopScoreLeafCollector::finishSegment() {
#if defined(__AVX512F__) || defined(__AVX2__)
    flushBatch();
#endif

    // Use scorer's total matches if available (for WAND and other early-terminating scorers)
    if (scorer_ && parent_ && scorerTracksTotalMatches_) {
        int totalMatches = scorer_->getTotalMatches();
        if (totalMatches >= 0) {
            // Scorer tracks total matches for this segment.
            // For WAND, this is the number of documents that were examined and matched,
            // which is a LOWER BOUND (WAND skips blocks without examining all docs).
            parent_->totalHits_ += totalMatches;

            // Mark as approximate since WAND provides lower bound
            parent_->totalHitsRelation_ = TotalHits::Relation::GREATER_THAN_OR_EQUAL_TO;
        }
    }
    // else: totalHits_ was already incremented correctly in collect()
}

void TopScoreDocCollector::TopScoreLeafCollector::collect(int doc) {
    if (!scorer_) {
        throw std::runtime_error("Scorer not set");
    }

    float score = scorer_->score();

    // Count hits for this segment:
    // - If scorer tracks matches, we'll replace this with scorer's count in finishSegment()
    // - Otherwise, this count is the accurate totalHits for this segment
    if (!scorerTracksTotalMatches_) {
        parent_->totalHits_++;
    }
    segmentHitsFromCollect_++;

    // Skip NaN and infinite scores (invalid)
    if (std::isnan(score) || std::isinf(score)) {
        return;
    }

    int globalDoc = docBase_ + doc;

    // Check pagination filter
    if (after_ != nullptr) {
        if (globalDoc < after_->doc) {
            return;
        }
        if (globalDoc == after_->doc) {
            return;
        }
        if (score == after_->score && globalDoc <= after_->doc) {
            return;
        }
    }

#if defined(__AVX512F__) || defined(__AVX2__)
    // Add to batch (AVX512: 16 floats, AVX2: 8 floats)
    docBatch_[batchPos_] = globalDoc;
    scoreBatch_[batchPos_] = score;
    batchPos_++;

    // Flush batch when full
    if (batchPos_ >= BATCH_SIZE) {
        flushBatch();
    }
#else
    // Scalar fallback: process immediately
    collectSingle(globalDoc, score);
#endif
}

void TopScoreDocCollector::TopScoreLeafCollector::collectSingle(int globalDoc, float score) {
    ScoreDoc scoreDoc(globalDoc, score);

    if (static_cast<int>(parent_->pq_.size()) < parent_->numHits_) {
        // Queue not full yet, just add
        parent_->pq_.push(scoreDoc);

        // Check if queue just became full - update threshold
        if (static_cast<int>(parent_->pq_.size()) == parent_->numHits_) {
            updateMinCompetitiveScore();
        }
    } else {
        // Queue is full, check if this doc beats the worst doc
        const ScoreDoc& top = parent_->pq_.top();

        // Compare: higher score is better, lower doc ID breaks ties
        bool betterThanTop = (score > top.score) || (score == top.score && globalDoc < top.doc);

        if (betterThanTop) {
            parent_->pq_.pop();
            parent_->pq_.push(scoreDoc);

            // Threshold changed - update min competitive score
            updateMinCompetitiveScore();
        }
    }
}

void TopScoreDocCollector::TopScoreLeafCollector::updateMinCompetitiveScore() {
    if (!scorer_ || static_cast<int>(parent_->pq_.size()) < parent_->numHits_)
        return;

    // When we've exceeded the totalHitsThreshold, activate WAND early termination
    // by setting the min competitive score and marking hits as approximate
    if (parent_->totalHits_ > parent_->totalHitsThreshold_) {
        float minScore = parent_->pq_.top().score;
        if (minScore > 0.0f) {
            scorer_->setMinCompetitiveScore(minScore);
            parent_->totalHitsRelation_ = TotalHits::Relation::GREATER_THAN_OR_EQUAL_TO;
        }
    } else {
        // Below threshold: still set min competitive score for WAND feedback
        // but keep exact counting
        float minScore = parent_->pq_.top().score;
        scorer_->setMinCompetitiveScore(minScore);
    }
}

#if defined(__AVX512F__)
void TopScoreDocCollector::TopScoreLeafCollector::flushBatch() {
    if (batchPos_ == 0) {
        return;  // Nothing to flush
    }

    if (static_cast<int>(parent_->pq_.size()) < parent_->numHits_) {
        // Queue not full yet, add all documents from batch
        for (int i = 0; i < batchPos_; i++) {
            collectSingle(docBatch_[i], scoreBatch_[i]);
        }
    } else {
        // Queue is full, use AVX512 SIMD to filter (16 floats at a time)
        float minScore = parent_->pq_.top().score;
        __m512 minScore_vec = _mm512_set1_ps(minScore);

        // Load batch scores (16 floats)
        __m512 scores_vec = _mm512_loadu_ps(scoreBatch_);

        // Compare: which scores beat minimum?
        // AVX512 uses mask registers instead of movemask
        __mmask16 mask = _mm512_cmp_ps_mask(scores_vec, minScore_vec, _CMP_GT_OQ);

        // Process documents that beat minScore
        for (int i = 0; i < batchPos_; i++) {
            if (mask & (1 << i)) {
                // This document beats minScore, add to queue
                collectSingle(docBatch_[i], scoreBatch_[i]);
            }
        }
    }

    batchPos_ = 0;  // Reset batch
}
#elif defined(__AVX2__)
void TopScoreDocCollector::TopScoreLeafCollector::flushBatch() {
    if (batchPos_ == 0) {
        return;  // Nothing to flush
    }

    if (static_cast<int>(parent_->pq_.size()) < parent_->numHits_) {
        // Queue not full yet, add all documents from batch
        for (int i = 0; i < batchPos_; i++) {
            collectSingle(docBatch_[i], scoreBatch_[i]);
        }
    } else {
        // Queue is full, use AVX2 SIMD to filter (8 floats at a time)
        float minScore = parent_->pq_.top().score;
        __m256 minScore_vec = _mm256_set1_ps(minScore);

        // Load batch scores (8 floats)
        __m256 scores_vec = _mm256_loadu_ps(scoreBatch_);

        // Compare: which scores beat minimum?
        // mask[i] = 0xFFFFFFFF if scoreBatch_[i] > minScore, else 0
        __m256 mask = _mm256_cmp_ps(scores_vec, minScore_vec, _CMP_GT_OQ);

        // Extract mask as integer
        int mask_int = _mm256_movemask_ps(mask);

        // Process documents that beat minScore
        for (int i = 0; i < batchPos_; i++) {
            if (mask_int & (1 << i)) {
                // This document beats minScore, add to queue
                collectSingle(docBatch_[i], scoreBatch_[i]);
            }
        }
    }

    batchPos_ = 0;  // Reset batch
}
#endif

}  // namespace search
}  // namespace diagon
