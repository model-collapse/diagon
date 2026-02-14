// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/SindiScorer.h"

#include "diagon/util/SIMDUtils.h"

#include <algorithm>
#include <cstring>

// AVX2 intrinsics (conditional compilation)
#if defined(__AVX2__)
#    include <immintrin.h>
#endif

namespace diagon {
namespace sparse {

// ==================== AVX2 Implementation ====================

#if defined(__AVX2__)

void SindiScorer::accumulateScoresAVX2(const uint32_t* doc_ids, const float* doc_weights,
                                       size_t count, float query_weight, std::vector<float>& scores,
                                       bool use_prefetch) {
    if (count == 0)
        return;

    // Broadcast query weight to all 8 lanes
    __m256 query_weight_vec = _mm256_set1_ps(query_weight);

    size_t i = 0;

    // Process in chunks of 8 (AVX2 width)
    for (; i + AVX2_WIDTH <= count; i += AVX2_WIDTH) {
        // Prefetch next iteration's data
        if (use_prefetch && (i + PREFETCH_DISTANCE < count)) {
            util::simd::Prefetch::read(&doc_ids[i + PREFETCH_DISTANCE],
                                       util::simd::Prefetch::Locality::HIGH);
            util::simd::Prefetch::read(&doc_weights[i + PREFETCH_DISTANCE],
                                       util::simd::Prefetch::Locality::HIGH);
        }

        // Load 8 document IDs
        __m256i doc_id_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&doc_ids[i]));

        // Load 8 document weights
        __m256 doc_weight_vec = _mm256_loadu_ps(&doc_weights[i]);

        // Compute contribution: query_weight * doc_weight
        __m256 contribution = _mm256_mul_ps(query_weight_vec, doc_weight_vec);

        // Extract and accumulate scores (scatter operation)
        // Note: AVX2 doesn't have native scatter, so we extract to scalar
        alignas(32) uint32_t doc_id_array[8];
        alignas(32) float contribution_array[8];

        _mm256_store_si256(reinterpret_cast<__m256i*>(doc_id_array), doc_id_vec);
        _mm256_store_ps(contribution_array, contribution);

        // Accumulate to score array
        for (size_t j = 0; j < AVX2_WIDTH; ++j) {
            uint32_t doc_id = doc_id_array[j];
            if (doc_id < scores.size()) {
                scores[doc_id] += contribution_array[j];
            }
        }
    }

    // Handle remaining elements (tail)
    for (; i < count; ++i) {
        uint32_t doc_id = doc_ids[i];
        if (doc_id < scores.size()) {
            scores[doc_id] += query_weight * doc_weights[i];
        }
    }
}

#else  // No AVX2 support

void SindiScorer::accumulateScoresAVX2(const uint32_t* doc_ids, const float* doc_weights,
                                       size_t count, float query_weight, std::vector<float>& scores,
                                       bool use_prefetch) {
    // Fall back to scalar implementation
    (void)use_prefetch;  // Unused without AVX2
    accumulateScoresScalar(doc_ids, doc_weights, count, query_weight, scores);
}

#endif  // __AVX2__

// ==================== Scalar Implementation ====================

void SindiScorer::accumulateScoresScalar(const uint32_t* doc_ids, const float* doc_weights,
                                         size_t count, float query_weight,
                                         std::vector<float>& scores) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t doc_id = doc_ids[i];
        if (doc_id < scores.size()) {
            scores[doc_id] += query_weight * doc_weights[i];
        }
    }
}

// ==================== Dispatch ====================

void SindiScorer::accumulateScores(const uint32_t* doc_ids, const float* doc_weights, size_t count,
                                   float query_weight, std::vector<float>& scores, bool use_simd,
                                   bool use_prefetch) {
    if (use_simd && hasAVX2()) {
        accumulateScoresAVX2(doc_ids, doc_weights, count, query_weight, scores, use_prefetch);
    } else {
        accumulateScoresScalar(doc_ids, doc_weights, count, query_weight, scores);
    }
}

// ==================== Runtime Detection ====================

bool SindiScorer::hasAVX2() {
#if defined(__AVX2__)
    // Compile-time check: if compiled with -mavx2, assume available
    // For more robust runtime detection, could use CPUID
    return true;
#else
    return false;
#endif
}

bool SindiScorer::hasPrefetch() {
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
    // Prefetch intrinsics available on all modern compilers
    return true;
#else
    return false;
#endif
}

}  // namespace sparse
}  // namespace diagon
