// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/SIMDUtils.h"
#include <cmath>

namespace diagon {
namespace search {

/**
 * Batch BM25 scoring with SIMD
 *
 * Computes BM25 scores for multiple documents in parallel using AVX2/AVX512.
 *
 * ## BM25 Formula
 *
 * score = IDF * (freq * (k1 + 1)) / (freq + k1 * (1 - b + b * length / avgLength))
 *
 * where:
 * - IDF: Inverse document frequency (constant per term)
 * - freq: Term frequency in document
 * - k1: Term frequency saturation (default 1.2)
 * - b: Length normalization (default 0.75)
 * - length: Document length (decoded from norm)
 * - avgLength: Average document length (default 50.0)
 *
 * ## SIMD Strategy
 *
 * Process 8 documents at once with AVX2:
 * 1. Load 8 frequencies → __m256 (8 × float32)
 * 2. Load 8 norms → decode to lengths → __m256
 * 3. Compute denominator: freq + k * (1 - b + b * length / avgLength)
 * 4. Compute score: IDF * freq * (k1 + 1) / denominator
 *
 * All operations vectorized with AVX2 instructions.
 *
 * ## Performance
 *
 * Scalar: 8 docs × 20 cycles = 160 cycles
 * SIMD:   ~80 cycles for 8 docs
 * Speedup: 2× from parallel computation
 */
class BatchBM25Scorer {
public:
    /**
     * Decode Lucene norm to field length (vectorized)
     *
     * Encoding: norm = 127 / sqrt(length)
     * Decoding: length = (127 / norm)²
     *
     * @param norms Input norms [8] (int64)
     * @return Decoded lengths [8] (float32)
     */
#if defined(DIAGON_HAVE_AVX2)
    static __m256 decodeNormsBatch(const long* norms) {
        // Load 8 norms as int64 → convert to float
        // AVX2 doesn't have direct i64→f32, so we do it in two steps
        __m128 norm_lo = _mm_cvtepi32_ps(
            _mm_set_epi32(static_cast<int>(norms[3]), static_cast<int>(norms[2]),
                         static_cast<int>(norms[1]), static_cast<int>(norms[0])));
        __m128 norm_hi = _mm_cvtepi32_ps(
            _mm_set_epi32(static_cast<int>(norms[7]), static_cast<int>(norms[6]),
                         static_cast<int>(norms[5]), static_cast<int>(norms[4])));

        __m256 norm_vec = _mm256_set_m128(norm_hi, norm_lo);

        // Handle zero/default norms
        __m256 zero_mask = _mm256_cmp_ps(norm_vec, _mm256_setzero_ps(), _CMP_EQ_OQ);
        __m256 safe_norm = _mm256_blendv_ps(norm_vec, _mm256_set1_ps(127.0f), zero_mask);

        // length = (127 / norm)²
        __m256 ratio = _mm256_div_ps(_mm256_set1_ps(127.0f), safe_norm);
        __m256 length = _mm256_mul_ps(ratio, ratio);

        return length;
    }
#endif

    /**
     * Compute BM25 scores for batch of documents (AVX2)
     *
     * @param freqs Term frequencies [8]
     * @param norms Document norms [8]
     * @param idf IDF value (constant per term)
     * @param k1 Term frequency saturation (default 1.2)
     * @param b Length normalization (default 0.75)
     * @param avgLength Average document length (default 50.0)
     * @param scores Output scores [8]
     */
#if defined(DIAGON_HAVE_AVX2)
    static void scoreBatchAVX2(const int* freqs, const long* norms,
                               float idf, float k1, float b, float avgLength,
                               float* scores) {
        // Load 8 frequencies
        __m256 freq_vec = _mm256_cvtepi32_ps(_mm256_loadu_si256((__m256i*)freqs));

        // Decode 8 norms to lengths
        __m256 length_vec = decodeNormsBatch(norms);

        // Broadcast constants
        __m256 idf_vec = _mm256_set1_ps(idf);
        __m256 k1_vec = _mm256_set1_ps(k1);
        __m256 b_vec = _mm256_set1_ps(b);
        __m256 avgLen_vec = _mm256_set1_ps(avgLength);
        __m256 one_vec = _mm256_set1_ps(1.0f);
        __m256 zero_vec = _mm256_setzero_ps();

        // Handle zero frequencies
        __m256 freq_zero_mask = _mm256_cmp_ps(freq_vec, zero_vec, _CMP_EQ_OQ);

        // Compute k = k1 * (1 - b + b * length / avgLength)
        __m256 length_ratio = _mm256_div_ps(length_vec, avgLen_vec);
        __m256 b_term = _mm256_mul_ps(b_vec, length_ratio);
        __m256 one_minus_b = _mm256_sub_ps(one_vec, b_vec);
        __m256 k_factor = _mm256_add_ps(one_minus_b, b_term);
        __m256 k = _mm256_mul_ps(k1_vec, k_factor);

        // Compute numerator: freq * (k1 + 1)
        __m256 k1_plus_1 = _mm256_add_ps(k1_vec, one_vec);
        __m256 numerator = _mm256_mul_ps(freq_vec, k1_plus_1);

        // Compute denominator: freq + k
        __m256 denominator = _mm256_add_ps(freq_vec, k);

        // Compute raw score: idf * numerator / denominator
        __m256 score_vec = _mm256_mul_ps(idf_vec, _mm256_div_ps(numerator, denominator));

        // Zero out scores where freq == 0
        score_vec = _mm256_andnot_ps(freq_zero_mask, score_vec);

        // Store results
        _mm256_storeu_ps(scores, score_vec);
    }
#endif

    /**
     * Scalar fallback for non-AVX2 systems
     */
    static void scoreBatchScalar(const int* freqs, const long* norms,
                                 float idf, float k1, float b, float avgLength,
                                 float* scores) {
        for (int i = 0; i < 8; i++) {
            if (freqs[i] == 0) {
                scores[i] = 0.0f;
                continue;
            }

            // Decode norm to length
            long norm = norms[i];
            float length;
            if (norm == 0 || norm == 127) {
                length = 1.0f;
            } else {
                float ratio = 127.0f / static_cast<float>(norm);
                length = ratio * ratio;
            }

            // BM25 formula
            float k = k1 * (1.0f - b + b * length / avgLength);
            float numerator = freqs[i] * (k1 + 1.0f);
            float denominator = freqs[i] + k;
            scores[i] = idf * numerator / denominator;
        }
    }

    /**
     * Dispatch to best available implementation
     */
    static void scoreBatch(const int* freqs, const long* norms,
                          float idf, float k1, float b, float avgLength,
                          float* scores, int count) {
        if (count == 8) {
#if defined(DIAGON_HAVE_AVX2)
            scoreBatchAVX2(freqs, norms, idf, k1, b, avgLength, scores);
#else
            scoreBatchScalar(freqs, norms, idf, k1, b, avgLength, scores);
#endif
        } else {
            // Partial batch, use scalar
            scoreBatchScalar(freqs, norms, idf, k1, b, avgLength, scores);
        }
    }
};

}  // namespace search
}  // namespace diagon
