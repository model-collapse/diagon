// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/BatchPostingsEnum.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Batch PostingsEnum implementation for Lucene104 format
 *
 * P1.1 Optimization: Native batch decoding to eliminate virtual call overhead.
 *
 * ## Key Difference from Regular PostingsEnum
 *
 * **Regular (one-at-a-time)**:
 * - Call nextDoc() 8 times → 8 virtual calls
 * - Each call: check buffer, decode if needed, return 1 doc
 * - Cost: 8 × 15 cycles = 120 cycles
 *
 * **Batch (this implementation)**:
 * - Call nextBatch() once → 1 virtual call
 * - Decode 8 docs at once, return batch
 * - Cost: 15 + decode cycles = ~40 cycles
 *
 * ## Implementation Strategy
 *
 * Leverage existing StreamVByte infrastructure:
 * 1. Keep 32-doc buffer (8 StreamVByte groups)
 * 2. Expose batch interface to return 8 docs at once
 * 3. Use same refillBuffer() logic
 *
 * Expected improvement: 40 μs reduction from baseline
 */
class Lucene104PostingsEnumBatch : public index::BatchPostingsEnum {
public:
    /**
     * Constructor
     * @param docIn Input for reading doc IDs
     * @param termState Term state with file pointers
     * @param writeFreqs Whether frequencies are encoded
     */
    Lucene104PostingsEnumBatch(std::unique_ptr<store::IndexInput> docIn, const TermState& termState,
                               bool writeFreqs);

    // ==================== DocIdSetIterator ====================

    int docID() const override { return currentDoc_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return docFreq_; }

    // ==================== PostingsEnum ====================

    int freq() const override { return currentFreq_; }

    // ==================== BatchPostingsEnum ====================

    /**
     * Decode next batch of documents (native implementation)
     *
     * This is the key optimization: decodes up to batch.capacity documents
     * in one call, eliminating per-document virtual call overhead.
     *
     * @param batch Output batch (pre-allocated)
     * @return Number of documents decoded (0 = exhausted)
     */
    int nextBatch(index::PostingsBatch& batch) override;

private:
    std::unique_ptr<store::IndexInput> docIn_;  // Owned clone
    int docFreq_;
    int64_t totalTermFreq_;
    bool writeFreqs_;

    // Current state (for one-at-a-time compatibility)
    int currentDoc_;
    int currentFreq_;
    int docsRead_;

    // StreamVByte buffering (32 docs = 8 groups of 4)
    static constexpr int BUFFER_SIZE = 32;
    static constexpr int STREAMVBYTE_GROUP_SIZE = 4;
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;
    int bufferLimit_;

    /**
     * Refill buffer by reading StreamVByte groups
     *
     * Decodes up to 32 docs (8 groups of 4) in one shot.
     * Uses existing StreamVByte SIMD decode from P0.4.
     */
    void refillBuffer();
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
