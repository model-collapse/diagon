// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace diagon {
namespace sparse {

/**
 * SindiScorer - SIMD-optimized sparse vector scoring with prefetch
 *
 * Implements efficient score accumulation for sparse inverted index search:
 * - AVX2 vectorized processing (8x float operations)
 * - Software prefetch hints to reduce cache misses
 * - Scalar fallback for non-AVX2 platforms
 *
 * Based on SINDI paper: "SINDI: Efficient Inverted Index Using Block-Max SIMD"
 * (https://arxiv.org/html/2509.08395v2)
 *
 * Key optimizations:
 * 1. **SIMD**: Process 8 doc IDs and weights in parallel with AVX2
 * 2. **Prefetch**: Hint CPU to load next cache line before needed
 * 3. **Cache-friendly**: Sequential access pattern on posting lists
 *
 * Example usage:
 * ```cpp
 * std::vector<float> scores(num_docs, 0.0f);
 *
 * // Score accumulation for query term with weight 0.8
 * SindiScorer::accumulateScores(
 *     posting_doc_ids,    // Document IDs
 *     posting_weights,    // Term weights
 *     posting_length,     // Number of postings
 *     0.8f,              // Query weight
 *     scores,            // Output: scores[doc_id] += query_weight * doc_weight
 *     true,              // Use SIMD
 *     true               // Use prefetch
 * );
 * ```
 */
class SindiScorer {
public:
    // ==================== Configuration ====================

    /**
     * Prefetch distance (elements ahead)
     *
     * Prefetch next iteration's data while processing current iteration.
     * Optimal distance depends on:
     * - Memory latency (~100-300 cycles for DRAM)
     * - Computation intensity (~10-20 cycles per 8 elements)
     * - Cache line size (64 bytes = 8 uint32_t or 16 float)
     *
     * Default: 8 elements = 1 cache line ahead
     */
    static constexpr size_t PREFETCH_DISTANCE = 8;

    /**
     * AVX2 processing width (8 floats)
     */
    static constexpr size_t AVX2_WIDTH = 8;

    // ==================== SIMD Score Accumulation ====================

    /**
     * Accumulate scores using AVX2 with optional prefetch
     *
     * Processes posting list in chunks of 8 elements using AVX2 instructions.
     * For each (doc_id, weight) pair:
     *   scores[doc_id] += query_weight * weight
     *
     * @param doc_ids Document IDs in posting list (must be aligned naturally)
     * @param doc_weights Term weights for each document
     * @param count Number of postings to process
     * @param query_weight Weight of query term
     * @param scores Output score array (indexed by doc_id)
     * @param use_prefetch If true, prefetch next cache line ahead
     *
     * Requirements:
     * - doc_ids and doc_weights must have same length (count)
     * - scores must be large enough to hold max(doc_ids)
     * - AVX2 support required (compile with -mavx2)
     *
     * Performance: ~2-4× faster than scalar on modern x86-64
     */
    static void accumulateScoresAVX2(const uint32_t* doc_ids, const float* doc_weights,
                                     size_t count, float query_weight, std::vector<float>& scores,
                                     bool use_prefetch = true);

    // ==================== Scalar Fallback ====================

    /**
     * Scalar implementation for non-AVX2 platforms
     *
     * Simple loop implementation:
     *   for each (doc_id, weight):
     *     scores[doc_id] += query_weight * weight
     *
     * @param doc_ids Document IDs in posting list
     * @param doc_weights Term weights for each document
     * @param count Number of postings to process
     * @param query_weight Weight of query term
     * @param scores Output score array (indexed by doc_id)
     */
    static void accumulateScoresScalar(const uint32_t* doc_ids, const float* doc_weights,
                                       size_t count, float query_weight,
                                       std::vector<float>& scores);

    // ==================== Dispatch ====================

    /**
     * Dispatch to best available implementation
     *
     * Automatically selects:
     * - AVX2 implementation if use_simd=true and AVX2 available
     * - Scalar fallback otherwise
     *
     * @param doc_ids Document IDs in posting list
     * @param doc_weights Term weights for each document
     * @param count Number of postings to process
     * @param query_weight Weight of query term
     * @param scores Output score array (indexed by doc_id)
     * @param use_simd If true, use SIMD (AVX2) if available
     * @param use_prefetch If true, use prefetch hints (AVX2 only)
     *
     * Note: use_prefetch is ignored if use_simd=false or AVX2 unavailable
     */
    static void accumulateScores(const uint32_t* doc_ids, const float* doc_weights, size_t count,
                                 float query_weight, std::vector<float>& scores,
                                 bool use_simd = true, bool use_prefetch = true);

    // ==================== Utilities ====================

    /**
     * Check if AVX2 is available at runtime
     *
     * Uses CPUID to detect AVX2 support.
     * On non-x86 platforms, returns false.
     *
     * @return True if AVX2 instructions can be used
     */
    static bool hasAVX2();

    /**
     * Check if prefetch is supported
     *
     * Prefetch is supported on:
     * - x86/x86-64: SSE or later (all modern CPUs)
     * - ARM: ARMv8 or later
     *
     * @return True if prefetch instructions are available
     */
    static bool hasPrefetch();
};

}  // namespace sparse
}  // namespace diagon
