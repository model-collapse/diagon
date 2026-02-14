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
    __attribute__((always_inline)) static void scoreBatchAVX2(const int* freqs, const long* norms,
                                                              float idf, float k1, float b,
                                                              float avgLength, float* scores) {
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

        // Compute denominator: freq + k
        __m256 denominator = _mm256_add_ps(freq_vec, k);

        // Compute BM25 score (Lucene 8+ simplified): idf * freq / (freq + k)
        __m256 score_vec = _mm256_mul_ps(idf_vec, _mm256_div_ps(freq_vec, denominator));

        // Zero out scores where freq == 0
        score_vec = _mm256_andnot_ps(freq_zero_mask, score_vec);

        // Store results
        _mm256_storeu_ps(scores, score_vec);
    }
#endif

    // ==================== AVX512 Implementation ====================

#if defined(DIAGON_HAVE_AVX512)
    /**
     * Decode Lucene norms to field lengths (AVX512 - 16 documents)
     *
     * Encoding: norm = 127 / sqrt(length)
     * Decoding: length = (127 / norm)²
     *
     * @param norms Input norms [16] (int64)
     * @return Decoded lengths [16] (float32)
     */
    static __m512 decodeNormsBatchAVX512(const long* norms) {
        // Load 16 norms as int64 → convert to float
        // Convert to int32 first (norms are small values)
        __m256i norms_lo_i32 = _mm256_set_epi32(
            static_cast<int>(norms[7]), static_cast<int>(norms[6]), static_cast<int>(norms[5]),
            static_cast<int>(norms[4]), static_cast<int>(norms[3]), static_cast<int>(norms[2]),
            static_cast<int>(norms[1]), static_cast<int>(norms[0]));

        __m256i norms_hi_i32 = _mm256_set_epi32(
            static_cast<int>(norms[15]), static_cast<int>(norms[14]), static_cast<int>(norms[13]),
            static_cast<int>(norms[12]), static_cast<int>(norms[11]), static_cast<int>(norms[10]),
            static_cast<int>(norms[9]), static_cast<int>(norms[8]));

        // Convert to float
        __m256 norms_lo_f32 = _mm256_cvtepi32_ps(norms_lo_i32);
        __m256 norms_hi_f32 = _mm256_cvtepi32_ps(norms_hi_i32);

        // Combine into 512-bit vector
        __m512 norm_vec = _mm512_castps256_ps512(norms_lo_f32);
        norm_vec = _mm512_insertf32x8(norm_vec, norms_hi_f32, 1);

        // Handle zero/default norms (mask-based)
        __mmask16 zero_mask = _mm512_cmp_ps_mask(norm_vec, _mm512_setzero_ps(), _CMP_EQ_OQ);
        __m512 safe_norm = _mm512_mask_blend_ps(zero_mask, norm_vec, _mm512_set1_ps(127.0f));

        // length = (127 / norm)²
        __m512 ratio = _mm512_div_ps(_mm512_set1_ps(127.0f), safe_norm);
        __m512 length = _mm512_mul_ps(ratio, ratio);

        return length;
    }

    /**
     * Compute BM25 scores for 16 documents (AVX512)
     *
     * @param freqs Term frequencies [16]
     * @param norms Document norms [16]
     * @param idf IDF value (constant per term)
     * @param k1 Term frequency saturation (default 1.2)
     * @param b Length normalization (default 0.75)
     * @param avgLength Average document length (default 50.0)
     * @param scores Output scores [16]
     */
    __attribute__((always_inline)) static void scoreBatchAVX512(const int* freqs, const long* norms,
                                                                float idf, float k1, float b,
                                                                float avgLength, float* scores) {
        // Load 16 frequencies
        __m512 freq_vec = _mm512_cvtepi32_ps(_mm512_loadu_si512((__m512i*)freqs));

        // Decode 16 norms to lengths
        __m512 length_vec = decodeNormsBatchAVX512(norms);

        // Broadcast constants
        __m512 idf_vec = _mm512_set1_ps(idf);
        __m512 k1_vec = _mm512_set1_ps(k1);
        __m512 b_vec = _mm512_set1_ps(b);
        __m512 avgLen_vec = _mm512_set1_ps(avgLength);
        __m512 one_vec = _mm512_set1_ps(1.0f);
        __m512 zero_vec = _mm512_setzero_ps();

        // Handle zero frequencies with mask (AVX512 feature)
        __mmask16 freq_nonzero_mask = _mm512_cmp_ps_mask(freq_vec, zero_vec, _CMP_NEQ_OQ);

        // Compute k = k1 * (1 - b + b * length / avgLength)
        __m512 length_ratio = _mm512_div_ps(length_vec, avgLen_vec);
        __m512 b_term = _mm512_mul_ps(b_vec, length_ratio);
        __m512 one_minus_b = _mm512_sub_ps(one_vec, b_vec);
        __m512 k_factor = _mm512_add_ps(one_minus_b, b_term);
        __m512 k = _mm512_mul_ps(k1_vec, k_factor);

        // Compute denominator: freq + k
        __m512 denominator = _mm512_add_ps(freq_vec, k);

        // Compute BM25 score (Lucene 8+ simplified): idf * freq / (freq + k)
        __m512 score_vec = _mm512_mul_ps(idf_vec, _mm512_div_ps(freq_vec, denominator));

        // Zero out scores where freq == 0 (using mask)
        score_vec = _mm512_maskz_mov_ps(freq_nonzero_mask, score_vec);

        // Store results
        _mm512_storeu_ps(scores, score_vec);
    }
#endif  // DIAGON_HAVE_AVX512

    /**
     * Scalar fallback for non-SIMD systems or partial batches
     *
     * @param count Number of documents to score (variable)
     */
    __attribute__((always_inline)) static void scoreBatchScalar(const int* freqs, const long* norms,
                                                                float idf, float k1, float b,
                                                                float avgLength, float* scores,
                                                                int count) {
        for (int i = 0; i < count; i++) {
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

            // BM25 formula (Lucene 8+ simplified)
            float k = k1 * (1.0f - b + b * length / avgLength);
            float denominator = freqs[i] + k;
            scores[i] = idf * freqs[i] / denominator;
        }
    }

    /**
     * Dispatch to best available implementation based on batch size
     *
     * Automatically selects:
     * - AVX512 for count=16 (if available)
     * - AVX2 for count=8 (if available)
     * - Scalar for other counts or as fallback
     *
     * Phase 3.5: Inline hint for hot path optimization
     */
    __attribute__((always_inline)) static void scoreBatch(const int* freqs, const long* norms,
                                                          float idf, float k1, float b,
                                                          float avgLength, float* scores,
                                                          int count) {
#if defined(DIAGON_HAVE_AVX512)
        if (count == 16) {
            scoreBatchAVX512(freqs, norms, idf, k1, b, avgLength, scores);
        } else if (count == 8) {
            scoreBatchAVX2(freqs, norms, idf, k1, b, avgLength, scores);
        } else {
            scoreBatchScalar(freqs, norms, idf, k1, b, avgLength, scores, count);
        }
#elif defined(DIAGON_HAVE_AVX2)
        if (count == 8) {
            scoreBatchAVX2(freqs, norms, idf, k1, b, avgLength, scores);
        } else {
            scoreBatchScalar(freqs, norms, idf, k1, b, avgLength, scores, count);
        }
#else
        scoreBatchScalar(freqs, norms, idf, k1, b, avgLength, scores, count);
#endif
    }
};

}  // namespace search
}  // namespace diagon
