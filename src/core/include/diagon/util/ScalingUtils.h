// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace diagon {
namespace util {

/**
 * Utilities for scaling floating-point scores to integers.
 *
 * Based on: org.apache.lucene.search.WANDScorer
 * Lines: 57-120 (scalingFactor, scaleMaxScore, scaleMinScore)
 *
 * Purpose:
 * - Avoid floating-point precision errors in score comparisons
 * - Use exact integer arithmetic for WAND thresholds
 * - Guarantee: scaledSum >= scaledThreshold ⟹ floatSum >= floatThreshold
 *
 * How it works:
 * 1. Choose scalingFactor to bring float into range [2^23, 2^24)
 * 2. scaleMaxScore(): Round UP (avoid missing matches)
 * 3. scaleMinScore(): Round DOWN (be conservative)
 * 4. All comparisons use integers (exact, no precision loss)
 */
class ScalingUtils {
public:
    // Float mantissa is 24 bits (23 explicit + 1 implicit)
    static constexpr int FLOAT_MANTISSA_BITS = 24;

    // Maximum scaled score: 2^24 - 1 (fits in 24-bit mantissa)
    static constexpr int64_t MAX_SCALED_SCORE = (1L << 24) - 1;

    /**
     * Compute scaling factor for a float.
     *
     * Returns exponent E such that: f × 2^E ∈ [2^23, 2^24)
     *
     * Formula: FLOAT_MANTISSA_BITS - 1 - exponent
     *
     * Special cases:
     * - scalingFactor(0) = scalingFactor(MIN_VALUE) + 1
     * - scalingFactor(+Infty) = scalingFactor(MAX_VALUE) - 1
     *
     * Based on: WANDScorer.java:69-83
     */
    static int scalingFactor(float f) {
        if (f < 0) {
            throw std::invalid_argument("Scores must be positive or null");
        } else if (f == 0) {
            // Special case: 0 gets one more than MIN_VALUE
            return scalingFactor(std::numeric_limits<float>::min()) + 1;
        } else if (std::isinf(f)) {
            // Special case: +Infty gets one less than MAX_VALUE
            return scalingFactor(std::numeric_limits<float>::max()) - 1;
        } else {
            // Normal case: extract exponent
            double d = static_cast<double>(f);

            // Extract exponent using frexp: d = mantissa × 2^exponent
            // where mantissa ∈ [0.5, 1.0)
            int exponent;
            std::frexp(d, &exponent);

            // Formula from Lucene:
            // scalingFactor = FLOAT_MANTISSA_BITS - 1 - Math.getExponent(d)
            // In C++: Math.getExponent(d) = exponent - 1 (frexp returns exponent+1)
            return FLOAT_MANTISSA_BITS - 1 - (exponent - 1);
        }
    }

    /**
     * Scale max score to integer (ROUND UP).
     *
     * Result: maxScore × 2^scalingFactor, rounded up to nearest integer.
     *
     * Rounding UP ensures we don't miss any matches:
     * If true max score is 1.001 but we scale to 1, we might skip competitive docs.
     *
     * Clamped to MAX_SCALED_SCORE to avoid overflow.
     *
     * Based on: WANDScorer.java:90-105
     */
    static int64_t scaleMaxScore(float maxScore, int scalingFactor) {
        // Preconditions (debug assertions)
        if (std::isnan(maxScore)) {
            throw std::invalid_argument("Max score cannot be NaN");
        }
        if (maxScore < 0) {
            throw std::invalid_argument("Max score must be non-negative");
        }

        // Scale using ldexp: maxScore × 2^scalingFactor
        // Use double for intermediate to avoid float precision loss
        double scaled = std::ldexp(static_cast<double>(maxScore), scalingFactor);

        // Clamp to MAX_SCALED_SCORE
        if (scaled > MAX_SCALED_SCORE) {
            // Happens if:
            // - Scorer returns +Infinity as max score
            // - Scorer returns inconsistent max scores (local > global)
            return MAX_SCALED_SCORE;
        }

        // Round UP to ensure we don't miss matches
        // Cast is safe since scaled < 2^24
        return static_cast<int64_t>(std::ceil(scaled));
    }

    /**
     * Scale min competitive score to integer (ROUND DOWN).
     *
     * Result: minScore × 2^scalingFactor, rounded down to nearest integer.
     *
     * Rounding DOWN is conservative:
     * If true min is 1.001 and we scale to 2, we might keep docs we could skip.
     * But if we round down to 1, we're safe (never skip competitive docs).
     *
     * Based on: WANDScorer.java:111-120
     */
    static int64_t scaleMinScore(float minScore, int scalingFactor) {
        // Preconditions
        if (!std::isfinite(minScore)) {
            throw std::invalid_argument("Min score must be finite");
        }
        if (minScore < 0) {
            throw std::invalid_argument("Min score must be non-negative");
        }

        // Scale using ldexp
        double scaled = std::ldexp(static_cast<double>(minScore), scalingFactor);

        // Round DOWN (conservative)
        // If scaled > LONG_MAX, cast will overflow, but that's fine
        // (it means threshold is extremely high, we'll never reach it)
        return static_cast<int64_t>(std::floor(scaled));
    }
};

}  // namespace util
}  // namespace diagon
