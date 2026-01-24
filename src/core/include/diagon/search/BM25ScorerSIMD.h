// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/PostingsEnum.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/Scorer.h"

#ifdef DIAGON_HAVE_AVX2
#    include <immintrin.h>
#endif

#include <cmath>
#include <memory>

namespace diagon {
namespace search {

/**
 * SIMD-optimized BM25 scorer using AVX2 instructions
 *
 * Processes 8 documents at a time using 256-bit SIMD vectors.
 * Falls back to scalar implementation on non-AVX2 CPUs.
 *
 * Expected speedup: 4-8x on AVX2-capable hardware
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
     */
    BM25ScorerSIMD(const Weight& weight, std::unique_ptr<index::PostingsEnum> postings, float idf,
                   float k1 = 1.2f, float b = 0.75f);

    // Scorer interface
    int nextDoc() override;
    int docID() const override;
    float score() const override;

    // DocIdSetIterator interface
    int advance(int target) override;
    int64_t cost() const override;

    // Scorer interface
    const Weight& getWeight() const override;

#ifdef DIAGON_HAVE_AVX2
    /**
     * Batch score 8 documents using AVX2 SIMD
     * @param freqs Array of 8 term frequencies
     * @param norms Array of 8 document norms (encoded lengths)
     * @param scores Output array of 8 scores
     */
    void scoreBatch(const int* freqs, const long* norms, float* scores) const;

    /**
     * Score 8 frequencies with same norm (common case)
     * @param freqs Array of 8 term frequencies
     * @param norm Single norm value for all documents
     * @param scores Output array of 8 scores
     */
    void scoreBatchUniformNorm(const int* freqs, long norm, float* scores) const;
#endif

private:
    const Weight& weight_;
    std::unique_ptr<index::PostingsEnum> postings_;
    int doc_;
    float currentScore_;

    // BM25 parameters
    float idf_;
    float k1_;
    float b_;
    float k1_plus_1_;  // Precomputed k1 + 1

    /**
     * Scalar BM25 scoring (fallback and single-doc case)
     */
    float scoreScalar(int freq, long norm) const;

#ifdef DIAGON_HAVE_AVX2
    // Precomputed SIMD constants
    __m256 idf_vec_;
    __m256 k1_vec_;
    __m256 b_vec_;
    __m256 k1_plus_1_vec_;
    __m256 one_minus_b_vec_;
    __m256 avgFieldLength_vec_;

    /**
     * Decode 8 norms to field lengths (vectorized)
     */
    __m256 decodeNormsVec(const __m256i norms_vec) const;

    /**
     * Convert 8 integers to 8 floats (vectorized)
     */
    __m256 int32ToFloat(__m256i int_vec) const;
#endif
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
