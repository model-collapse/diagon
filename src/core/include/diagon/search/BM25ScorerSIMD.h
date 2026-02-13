// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/DocValues.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/Scorer.h"

// Platform-specific SIMD headers
#ifdef DIAGON_HAVE_AVX2
#    include <immintrin.h>
#    define DIAGON_BM25_BATCH_SIZE 8  // AVX2: 8 floats
#elif defined(DIAGON_HAVE_NEON)
#    include <arm_neon.h>
#    define DIAGON_BM25_BATCH_SIZE 4  // NEON: 4 floats
#else
#    define DIAGON_BM25_BATCH_SIZE 1  // Scalar fallback
#endif

#include <cmath>
#include <memory>

namespace diagon {
namespace search {

/**
 * SIMD-optimized BM25 scorer using AVX2 or ARM NEON instructions
 *
 * Platform support:
 * - AVX2 (x86-64): Processes 8 documents at a time (256-bit vectors)
 * - NEON (ARM64): Processes 4 documents at a time (128-bit vectors)
 * - Scalar: Fallback for unsupported platforms
 *
 * Expected speedup: 4-8x on SIMD-capable hardware
 *
 * BM25 formula vectorized:
 *   score[i] = idf * freq[i] * (k1 + 1) / (freq[i] + k[i])
 *   where k[i] = k1 * (1 - b + b * fieldLength[i] / avgFieldLength)
 */
class BM25ScorerSIMD : public Scorer {
public:
    /**
     * Constructor
     * @param weight Parent weight (for explain)
     * @param postings Postings iterator (takes ownership)
     * @param idf IDF component (precomputed)
     * @param k1 BM25 k1 parameter (default 1.2)
     * @param b BM25 b parameter (default 0.75)
     * @param avgFieldLength Average field length (for max score computation)
     * @param norms Document norms for length normalization (non-owning)
     */
    BM25ScorerSIMD(const Weight& weight, std::unique_ptr<index::PostingsEnum> postings, float idf,
                   float k1, float b, float avgFieldLength, index::NumericDocValues* norms);

    // Scorer interface
    int nextDoc() override;
    int docID() const override;
    float score() const override;

    // DocIdSetIterator interface
    int advance(int target) override;
    int64_t cost() const override;

    // Scorer interface
    const Weight& getWeight() const override;

    // Score upper bounds (P1: WAND optimization)
    float getMaxScore(int upTo) const override;
    int advanceShallow(int target) override;

#if defined(DIAGON_HAVE_AVX2) || defined(DIAGON_HAVE_NEON)
    /**
     * Batch score documents using SIMD
     * - AVX2: Processes 8 documents at a time
     * - NEON: Processes 4 documents at a time
     *
     * @param freqs Array of DIAGON_BM25_BATCH_SIZE term frequencies
     * @param norms Array of DIAGON_BM25_BATCH_SIZE document norms (encoded lengths)
     * @param scores Output array of DIAGON_BM25_BATCH_SIZE scores
     */
    void scoreBatch(const int* freqs, const long* norms, float* scores) const;

    /**
     * Score frequencies with same norm (common case)
     *
     * @param freqs Array of DIAGON_BM25_BATCH_SIZE term frequencies
     * @param norm Single norm value for all documents
     * @param scores Output array of DIAGON_BM25_BATCH_SIZE scores
     */
    void scoreBatchUniformNorm(const int* freqs, long norm, float* scores) const;
#endif

private:
    const Weight& weight_;
    std::unique_ptr<index::PostingsEnum> postings_;
    index::NumericDocValues* norms_;  // Non-owning pointer to norms
    int doc_;
    float currentScore_;

    // BM25 parameters
    float idf_;
    float k1_;
    float b_;
    float k1_plus_1_;  // Precomputed k1 + 1
    float avgFieldLength_;  // Average field length (for getMaxScore)

    /**
     * Scalar BM25 scoring (fallback and single-doc case)
     */
    float scoreScalar(int freq, long norm) const;

#if defined(DIAGON_HAVE_AVX2) || defined(DIAGON_HAVE_NEON)
    // Platform-specific SIMD types and functions
#ifdef DIAGON_HAVE_AVX2
    // AVX2: 256-bit vectors (8 floats)
    using FloatVec = __m256;
    using IntVec = __m256i;

    // Precomputed SIMD constants
    FloatVec idf_vec_;
    FloatVec k1_vec_;
    FloatVec b_vec_;
    FloatVec k1_plus_1_vec_;
    FloatVec one_minus_b_vec_;
    FloatVec avgFieldLength_vec_;

    FloatVec decodeNormsVec(const IntVec norms_vec) const;
    FloatVec int32ToFloat(IntVec int_vec) const;

#elif defined(DIAGON_HAVE_NEON)
    // NEON: 128-bit vectors (4 floats)
    using FloatVec = float32x4_t;
    using IntVec = int32x4_t;

    // Precomputed SIMD constants
    FloatVec idf_vec_;
    FloatVec k1_vec_;
    FloatVec b_vec_;
    FloatVec k1_plus_1_vec_;
    FloatVec one_minus_b_vec_;
    FloatVec avgFieldLength_vec_;

    FloatVec decodeNormsVec(const IntVec norms_vec) const;
    FloatVec int32ToFloat(IntVec int_vec) const;
#endif
#endif  // DIAGON_HAVE_AVX2 || DIAGON_HAVE_NEON
};

/**
 * Factory function to create optimal BM25 scorer
 * Automatically selects SIMD or scalar implementation
 */
std::unique_ptr<BM25ScorerSIMD> createBM25Scorer(const Weight& weight,
                                                 std::unique_ptr<index::PostingsEnum> postings,
                                                 float idf, float k1 = 1.2f, float b = 0.75f);

}  // namespace search
}  // namespace diagon
