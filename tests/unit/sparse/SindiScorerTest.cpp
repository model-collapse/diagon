// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/SindiScorer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace diagon::sparse;

// ==================== Scalar Accumulation Tests ====================

TEST(SindiScorerTest, ScalarAccumulationBasic) {
    // Simple accumulation: 3 postings
    uint32_t doc_ids[] = {0, 2, 5};
    float doc_weights[] = {1.0f, 2.0f, 3.0f};
    float query_weight = 0.5f;

    std::vector<float> scores(10, 0.0f);

    SindiScorer::accumulateScoresScalar(doc_ids, doc_weights, 3, query_weight, scores);

    // Expected: scores[0] = 0.5, scores[2] = 1.0, scores[5] = 1.5
    EXPECT_FLOAT_EQ(0.5f, scores[0]);
    EXPECT_FLOAT_EQ(0.0f, scores[1]);
    EXPECT_FLOAT_EQ(1.0f, scores[2]);
    EXPECT_FLOAT_EQ(0.0f, scores[3]);
    EXPECT_FLOAT_EQ(0.0f, scores[4]);
    EXPECT_FLOAT_EQ(1.5f, scores[5]);
}

TEST(SindiScorerTest, ScalarAccumulationMultipleTerms) {
    std::vector<float> scores(10, 0.0f);

    // Term 1: doc 0, 2, 5
    uint32_t doc_ids_1[] = {0, 2, 5};
    float doc_weights_1[] = {1.0f, 2.0f, 3.0f};
    SindiScorer::accumulateScoresScalar(doc_ids_1, doc_weights_1, 3, 0.5f, scores);

    // Term 2: doc 0, 3, 5
    uint32_t doc_ids_2[] = {0, 3, 5};
    float doc_weights_2[] = {2.0f, 1.0f, 1.0f};
    SindiScorer::accumulateScoresScalar(doc_ids_2, doc_weights_2, 3, 1.0f, scores);

    // Expected:
    // scores[0] = 0.5*1.0 + 1.0*2.0 = 2.5
    // scores[2] = 0.5*2.0 = 1.0
    // scores[3] = 1.0*1.0 = 1.0
    // scores[5] = 0.5*3.0 + 1.0*1.0 = 2.5
    EXPECT_FLOAT_EQ(2.5f, scores[0]);
    EXPECT_FLOAT_EQ(1.0f, scores[2]);
    EXPECT_FLOAT_EQ(1.0f, scores[3]);
    EXPECT_FLOAT_EQ(2.5f, scores[5]);
}

TEST(SindiScorerTest, ScalarAccumulationEmpty) {
    std::vector<float> scores(10, 0.0f);

    // Empty arrays - use nullptr since count is 0
    SindiScorer::accumulateScoresScalar(nullptr, nullptr, 0, 1.0f, scores);

    // All scores should remain 0
    for (float score : scores) {
        EXPECT_FLOAT_EQ(0.0f, score);
    }
}

TEST(SindiScorerTest, ScalarAccumulationOutOfBounds) {
    std::vector<float> scores(5, 0.0f);

    // Doc ID 10 is out of bounds (scores size = 5)
    uint32_t doc_ids[] = {1, 10, 3};
    float doc_weights[] = {1.0f, 2.0f, 3.0f};

    SindiScorer::accumulateScoresScalar(doc_ids, doc_weights, 3, 1.0f, scores);

    // Only docs 1 and 3 should be accumulated
    EXPECT_FLOAT_EQ(0.0f, scores[0]);
    EXPECT_FLOAT_EQ(1.0f, scores[1]);
    EXPECT_FLOAT_EQ(0.0f, scores[2]);
    EXPECT_FLOAT_EQ(3.0f, scores[3]);
    EXPECT_FLOAT_EQ(0.0f, scores[4]);
}

// ==================== AVX2 vs Scalar Correctness ====================

TEST(SindiScorerTest, AVX2MatchesScalar) {
    if (!SindiScorer::hasAVX2()) {
        GTEST_SKIP() << "AVX2 not available";
    }

    // Create posting list with 16 elements (2 AVX2 iterations)
    std::vector<uint32_t> doc_ids;
    std::vector<float> doc_weights;

    for (uint32_t i = 0; i < 16; ++i) {
        doc_ids.push_back(i * 2);  // Even doc IDs: 0, 2, 4, ..., 30
        doc_weights.push_back(static_cast<float>(i) * 0.1f);
    }

    float query_weight = 0.8f;

    // Accumulate with AVX2
    std::vector<float> scores_avx2(50, 0.0f);
    SindiScorer::accumulateScoresAVX2(doc_ids.data(), doc_weights.data(), doc_ids.size(),
                                      query_weight, scores_avx2, true);

    // Accumulate with scalar
    std::vector<float> scores_scalar(50, 0.0f);
    SindiScorer::accumulateScoresScalar(doc_ids.data(), doc_weights.data(), doc_ids.size(),
                                        query_weight, scores_scalar);

    // Compare results
    for (size_t i = 0; i < scores_avx2.size(); ++i) {
        EXPECT_NEAR(scores_scalar[i], scores_avx2[i], 1e-5f) << "Mismatch at index " << i;
    }
}

TEST(SindiScorerTest, AVX2WithPrefetch) {
    if (!SindiScorer::hasAVX2()) {
        GTEST_SKIP() << "AVX2 not available";
    }

    std::vector<uint32_t> doc_ids;
    std::vector<float> doc_weights;

    for (uint32_t i = 0; i < 100; ++i) {
        doc_ids.push_back(i);
        doc_weights.push_back(static_cast<float>(i) * 0.01f);
    }

    float query_weight = 1.5f;

    // With prefetch
    std::vector<float> scores_with_prefetch(150, 0.0f);
    SindiScorer::accumulateScoresAVX2(doc_ids.data(), doc_weights.data(), doc_ids.size(),
                                      query_weight, scores_with_prefetch, true);

    // Without prefetch
    std::vector<float> scores_without_prefetch(150, 0.0f);
    SindiScorer::accumulateScoresAVX2(doc_ids.data(), doc_weights.data(), doc_ids.size(),
                                      query_weight, scores_without_prefetch, false);

    // Results should be identical (prefetch is just a performance hint)
    for (size_t i = 0; i < scores_with_prefetch.size(); ++i) {
        EXPECT_FLOAT_EQ(scores_without_prefetch[i], scores_with_prefetch[i]);
    }
}

// ==================== Dispatch Tests ====================

TEST(SindiScorerTest, DispatchUsesSIMD) {
    if (!SindiScorer::hasAVX2()) {
        GTEST_SKIP() << "AVX2 not available";
    }

    std::vector<uint32_t> doc_ids = {0, 1, 2, 3, 4, 5, 6, 7};
    std::vector<float> doc_weights = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float query_weight = 2.0f;

    std::vector<float> scores(10, 0.0f);

    SindiScorer::accumulateScores(doc_ids.data(), doc_weights.data(), doc_ids.size(), query_weight,
                                  scores, true, true);

    // All docs should have score 2.0
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(2.0f, scores[i]);
    }
}

TEST(SindiScorerTest, DispatchFallsBackToScalar) {
    std::vector<uint32_t> doc_ids = {0, 1, 2, 3};
    std::vector<float> doc_weights = {1.0f, 2.0f, 3.0f, 4.0f};
    float query_weight = 0.5f;

    std::vector<float> scores(10, 0.0f);

    // Explicitly disable SIMD
    SindiScorer::accumulateScores(doc_ids.data(), doc_weights.data(), doc_ids.size(), query_weight,
                                  scores, false, false);

    EXPECT_FLOAT_EQ(0.5f, scores[0]);
    EXPECT_FLOAT_EQ(1.0f, scores[1]);
    EXPECT_FLOAT_EQ(1.5f, scores[2]);
    EXPECT_FLOAT_EQ(2.0f, scores[3]);
}

// ==================== Runtime Detection Tests ====================

TEST(SindiScorerTest, RuntimeDetection) {
    // These should not crash
    bool has_avx2 = SindiScorer::hasAVX2();
    bool has_prefetch = SindiScorer::hasPrefetch();

    // Prefetch should always be available on modern platforms
    EXPECT_TRUE(has_prefetch);

    // AVX2 availability depends on hardware
    std::cout << "AVX2 available: " << (has_avx2 ? "yes" : "no") << std::endl;
}

// ==================== Large Posting List Tests ====================

TEST(SindiScorerTest, LargePostingList) {
    const size_t num_postings = 10000;

    std::vector<uint32_t> doc_ids;
    std::vector<float> doc_weights;

    doc_ids.reserve(num_postings);
    doc_weights.reserve(num_postings);

    for (size_t i = 0; i < num_postings; ++i) {
        doc_ids.push_back(static_cast<uint32_t>(i));
        doc_weights.push_back(1.0f);
    }

    float query_weight = 0.1f;

    std::vector<float> scores(num_postings, 0.0f);

    SindiScorer::accumulateScores(doc_ids.data(), doc_weights.data(), doc_ids.size(), query_weight,
                                  scores, true, true);

    // All docs should have score 0.1
    for (size_t i = 0; i < num_postings; ++i) {
        EXPECT_FLOAT_EQ(0.1f, scores[i]);
    }
}
