// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/PostingsEnum.h"

#include <cstdint>
#include <vector>

namespace diagon {
namespace index {

/**
 * Batch for decoded postings
 *
 * Stores decoded document IDs and frequencies in contiguous arrays
 * for efficient batch processing with SIMD.
 */
struct PostingsBatch {
    int* docs;     // Document IDs [capacity]
    int* freqs;    // Term frequencies [capacity]
    int count;     // Actual number of documents in batch
    int capacity;  // Maximum batch size

    PostingsBatch(int cap)
        : count(0)
        , capacity(cap) {
        docs = new int[capacity];
        freqs = new int[capacity];
    }

    ~PostingsBatch() {
        delete[] docs;
        delete[] freqs;
    }

    // Disable copy, enable move
    PostingsBatch(const PostingsBatch&) = delete;
    PostingsBatch& operator=(const PostingsBatch&) = delete;
    PostingsBatch(PostingsBatch&&) = default;
    PostingsBatch& operator=(PostingsBatch&&) = default;
};

/**
 * BatchPostingsEnum - Batch-at-a-time postings interface
 *
 * Extends PostingsEnum with batch decoding capability to amortize
 * virtual call overhead and enable SIMD processing.
 *
 * ## Motivation
 *
 * Phase 4 analysis showed that one-at-a-time iteration creates
 * 32.79% overhead from virtual function calls, preventing SIMD
 * optimization of BM25 scoring.
 *
 * Batch decoding:
 * - Amortizes virtual call cost across N documents
 * - Enables SIMD BM25 scoring (8 docs at once with AVX2)
 * - Allows prefetching of norm values
 * - Matches QBlock's batch processing pattern
 *
 * ## Performance Model
 *
 * One-at-a-time: 8 docs × (2 virtual calls + scalar BM25) = 400 cycles
 * Batch-at-a-time: 1 virtual call + decode 8 + SIMD BM25 = 135 cycles
 * Expected speedup: 2.96×
 *
 * ## Example
 *
 * ```cpp
 * BatchPostingsEnum* batchPostings = ...;
 * PostingsBatch batch(8);  // Batch size = 8 for AVX2
 *
 * while (true) {
 *     int count = batchPostings->nextBatch(batch);
 *     if (count == 0) break;  // Exhausted
 *
 *     // Process batch with SIMD
 *     __m256 scores = simd_bm25_score(batch.freqs, norms, ...);
 *
 *     for (int i = 0; i < count; i++) {
 *         collector->collect(batch.docs[i], scores[i]);
 *     }
 * }
 * ```
 */
class BatchPostingsEnum : public PostingsEnum {
public:
    virtual ~BatchPostingsEnum() = default;

    /**
     * Decode next batch of postings
     *
     * Fills batch with up to batch.capacity documents. Returns actual
     * number decoded (may be less than capacity at end of postings).
     *
     * @param batch Output batch (pre-allocated)
     * @return Number of documents decoded (0 = exhausted)
     *
     * Implementation notes:
     * - Should use StreamVByte SIMD decoding internally
     * - Should prefetch next block for pipelining
     * - Should minimize branches in hot loop
     */
    virtual int nextBatch(PostingsBatch& batch) = 0;

    // ==================== Legacy One-at-a-Time API ====================

    /**
     * Implement one-at-a-time using batch underneath
     *
     * This provides backward compatibility while still benefiting
     * from batch decoding at the lower level.
     */
    int nextDoc() override {
        if (batch_pos_ >= batch_count_) {
            // Refill batch
            batch_count_ = nextBatch(internal_batch_);
            batch_pos_ = 0;

            if (batch_count_ == 0) {
                doc_ = NO_MORE_DOCS;
                return doc_;
            }
        }

        doc_ = internal_batch_.docs[batch_pos_];
        freq_ = internal_batch_.freqs[batch_pos_];
        batch_pos_++;

        return doc_;
    }

    int freq() const override { return freq_; }

protected:
    // Internal batch buffer for one-at-a-time compatibility
    PostingsBatch internal_batch_{8};
    int batch_pos_ = 0;
    int batch_count_ = 0;
    int doc_ = -1;
    int freq_ = 0;
};

}  // namespace index
}  // namespace diagon
