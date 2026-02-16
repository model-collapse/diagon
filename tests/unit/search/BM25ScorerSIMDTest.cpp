// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/BM25ScorerSIMD.h"

#include "diagon/index/LeafReaderContext.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/Weight.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace diagon::search;

// Helper class for testing
class TestDummyWeight : public Weight {
public:
    std::unique_ptr<Scorer> scorer(const diagon::index::LeafReaderContext&) const override {
        return nullptr;
    }
    const Query& getQuery() const override {
        static TermQuery dummyQuery(diagon::search::Term("", ""));
        return dummyQuery;
    }
};

class BM25ScorerSIMDTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default BM25 parameters
        k1_ = 1.2f;
        b_ = 0.75f;
        idf_ = 2.5f;  // Example IDF value
    }

    /**
     * Compute expected BM25 score using BM25Similarity scalar formula
     * (Lucene 8+ simplified: no (k1+1) in numerator)
     */
    float computeExpectedScore(int freq, long norm = 1L) const {
        if (freq == 0)
            return 0.0f;

        float fieldLength = 1.0f;
        float avgFieldLength = 1.0f;

        float k = k1_ * (1.0f - b_ + b_ * fieldLength / avgFieldLength);
        float freqFloat = static_cast<float>(freq);
        return idf_ * freqFloat / (freqFloat + k);
    }

    /**
     * Compute expected BM25 score using BM25ScorerSIMD formula
     * (Classic BM25 with (k1+1) in numerator, matching SIMD batch scorer)
     */
    float computeExpectedSIMDScore(int freq, long norm = 1L) const {
        if (freq == 0)
            return 0.0f;

        float fieldLength = 1.0f;
        float avgFieldLength = 1.0f;

        float k = k1_ * (1.0f - b_ + b_ * fieldLength / avgFieldLength);
        float freqFloat = static_cast<float>(freq);
        return idf_ * freqFloat * (k1_ + 1.0f) / (freqFloat + k);
    }

    /**
     * Check if two floats are approximately equal
     */
    bool approxEqual(float a, float b, float epsilon = 1e-4f) const {
        return std::abs(a - b) < epsilon;
    }

    float k1_;
    float b_;
    float idf_;
};

// ==================== Basic Correctness Tests ====================

TEST_F(BM25ScorerSIMDTest, ScalarScoring) {
    // Test scalar scoring matches expected formula
    std::vector<int> frequencies = {0, 1, 2, 5, 10, 20, 50, 100};

    for (int freq : frequencies) {
        float expected = computeExpectedScore(freq);

        // Compute using BM25Similarity for reference
        // Note: BM25Similarity.score() returns the frequency-dependent part only,
        // IDF is applied separately, so we multiply by IDF here
        BM25Similarity similarity(k1_, b_);
        float actual = idf_ * similarity.score(static_cast<float>(freq), 1L);

        EXPECT_TRUE(approxEqual(expected, actual))
            << "freq=" << freq << ", expected=" << expected << ", actual=" << actual;
    }
}

#if defined(DIAGON_HAVE_AVX2) || defined(DIAGON_HAVE_NEON)

TEST_F(BM25ScorerSIMDTest, SIMDCorrectnessVsScalar) {
    // Test SIMD scoring matches scalar scoring

    TestDummyWeight weight;

    // Create scorer
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    // Test frequencies (batch size depends on platform)
    constexpr int BATCH = DIAGON_BM25_BATCH_SIZE;
    alignas(32) int freqs[BATCH];
    alignas(32) long norms[BATCH];
    alignas(32) float scores[BATCH];

    // Initialize test data
    for (int i = 0; i < BATCH; ++i) {
        freqs[i] = (i + 1) * 10;  // 10, 20, 30, ...
        norms[i] = 1;
    }

    // Compute SIMD scores
    scorer->scoreBatch(freqs, norms, scores);

    // Verify against classic BM25 formula (SIMD scorer uses (k1+1) factor)
    for (int i = 0; i < BATCH; i++) {
        float expected = computeExpectedSIMDScore(freqs[i], norms[i]);
        EXPECT_TRUE(approxEqual(scores[i], expected))
            << "i=" << i << ", freq=" << freqs[i] << ", expected=" << expected
            << ", actual=" << scores[i];
    }
}

TEST_F(BM25ScorerSIMDTest, SIMDUniformNorm) {
    // Test uniform norm optimization

    TestDummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    alignas(32) int freqs[DIAGON_BM25_BATCH_SIZE] = {};
    alignas(32) float scores[DIAGON_BM25_BATCH_SIZE];
    // Use varied frequencies up to batch size
    constexpr int test_freqs[] = {1, 2, 3, 5, 10, 20, 50, 100};
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++)
        freqs[i] = test_freqs[i % 8];

    // Compute with uniform norm
    scorer->scoreBatchUniformNorm(freqs, 1L, scores);

    // Verify against classic BM25 formula (SIMD scorer uses (k1+1) factor)
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        float expected = computeExpectedSIMDScore(freqs[i], 1L);
        EXPECT_TRUE(approxEqual(scores[i], expected))
            << "i=" << i << ", freq=" << freqs[i] << ", expected=" << expected
            << ", actual=" << scores[i];
    }
}

TEST_F(BM25ScorerSIMDTest, ZeroFrequencies) {
    // Test that zero frequencies produce zero scores

    TestDummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    alignas(32) int freqs[DIAGON_BM25_BATCH_SIZE] = {};
    alignas(32) long norms[DIAGON_BM25_BATCH_SIZE];
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++)
        norms[i] = 1;
    alignas(32) float scores[DIAGON_BM25_BATCH_SIZE];

    scorer->scoreBatch(freqs, norms, scores);

    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        EXPECT_FLOAT_EQ(scores[i], 0.0f) << "i=" << i;
    }
}

TEST_F(BM25ScorerSIMDTest, MixedFrequencies) {
    // Test mixed zero and non-zero frequencies

    TestDummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    alignas(32) int freqs[DIAGON_BM25_BATCH_SIZE] = {};
    alignas(32) long norms[DIAGON_BM25_BATCH_SIZE];
    // Mixed zero and non-zero frequencies
    constexpr int mixed_freqs[] = {0, 1, 0, 5, 0, 20, 0, 100};
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        freqs[i] = mixed_freqs[i % 8];
        norms[i] = 1;
    }
    alignas(32) float scores[DIAGON_BM25_BATCH_SIZE];

    scorer->scoreBatch(freqs, norms, scores);

    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        float expected = computeExpectedSIMDScore(freqs[i], norms[i]);
        EXPECT_TRUE(approxEqual(scores[i], expected))
            << "i=" << i << ", freq=" << freqs[i] << ", expected=" << expected
            << ", actual=" << scores[i];
    }
}

TEST_F(BM25ScorerSIMDTest, HighFrequencies) {
    // Test saturation behavior with high frequencies

    TestDummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    alignas(32) int freqs[DIAGON_BM25_BATCH_SIZE] = {};
    alignas(32) long norms[DIAGON_BM25_BATCH_SIZE];
    alignas(32) float scores[DIAGON_BM25_BATCH_SIZE];
    constexpr int high_freqs[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        freqs[i] = high_freqs[i % 8];
        norms[i] = 1;
    }

    scorer->scoreBatch(freqs, norms, scores);

    // BM25 should saturate at high frequencies
    // Verify saturation: score(2000) < 2 * score(1000)
    for (int i = 1; i < DIAGON_BM25_BATCH_SIZE; i++) {
        float ratio = scores[i] / scores[i - 1];
        EXPECT_LT(ratio, 2.0f) << "i=" << i << ", no saturation observed";
        EXPECT_GT(ratio, 1.0f) << "i=" << i << ", scores should increase";
    }

    // Verify against classic BM25 formula (SIMD scorer uses (k1+1) factor)
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        float expected = computeExpectedSIMDScore(freqs[i], norms[i]);
        EXPECT_TRUE(approxEqual(scores[i], expected)) << "i=" << i << ", freq=" << freqs[i];
    }
}

TEST_F(BM25ScorerSIMDTest, DifferentParameters) {
    // Test with different k1 and b parameters

    TestDummyWeight weight;

    // Test various parameter combinations
    std::vector<std::pair<float, float>> params = {
        {1.2f, 0.75f},  // Default
        {2.0f, 0.75f},  // High k1
        {1.2f, 0.0f},   // No length normalization
        {1.2f, 1.0f},   // Full length normalization
        {0.5f, 0.5f},   // Low k1 and b
    };

    alignas(32) int freqs[DIAGON_BM25_BATCH_SIZE] = {};
    alignas(32) long norms[DIAGON_BM25_BATCH_SIZE];
    alignas(32) float scores[DIAGON_BM25_BATCH_SIZE];
    constexpr int param_freqs[] = {1, 2, 3, 5, 10, 20, 50, 100};
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        freqs[i] = param_freqs[i % 8];
        norms[i] = 1;
    }

    for (const auto& [k1, b] : params) {
        auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1, b, 1.0f, nullptr);
        scorer->scoreBatch(freqs, norms, scores);

        // Verify against scalar with same parameters
        for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
            float k = k1 * (1.0f - b + b * 1.0f / 1.0f);
            float expected = idf_ * freqs[i] * (k1 + 1.0f) / (freqs[i] + k);

            EXPECT_TRUE(approxEqual(scores[i], expected))
                << "k1=" << k1 << ", b=" << b << ", i=" << i << ", freq=" << freqs[i]
                << ", expected=" << expected << ", actual=" << scores[i];
        }
    }
}

TEST_F(BM25ScorerSIMDTest, Alignment) {
    // Test that SIMD functions work with both aligned and unaligned data

    TestDummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    // Unaligned data (offset by 1 byte)
    std::vector<uint8_t> buffer(sizeof(int) * 8 + 1);
    int* unaligned_freqs = reinterpret_cast<int*>(buffer.data() + 1);

    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        unaligned_freqs[i] = (i + 1) * 10;
    }

    std::vector<long> norms(8, 1L);
    std::vector<float> scores(8);

    // Should work even with unaligned data (loadu instruction)
    scorer->scoreBatch(unaligned_freqs, norms.data(), scores.data());

    // Verify correctness (SIMD scorer uses classic BM25 with (k1+1) factor)
    for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
        float expected = computeExpectedSIMDScore(unaligned_freqs[i], norms[i]);
        EXPECT_TRUE(approxEqual(scores[i], expected))
            << "i=" << i << ", freq=" << unaligned_freqs[i];
    }
}

TEST_F(BM25ScorerSIMDTest, RandomData) {
    // Test with random frequencies

    TestDummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, nullptr, idf_, k1_, b_, 1.0f, nullptr);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> freq_dist(0, 100);

    // Test 100 batches of random data
    for (int batch = 0; batch < 100; batch++) {
        alignas(32) int freqs[DIAGON_BM25_BATCH_SIZE];
        alignas(32) long norms[DIAGON_BM25_BATCH_SIZE];
        alignas(32) float scores[DIAGON_BM25_BATCH_SIZE];

        for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
            freqs[i] = freq_dist(rng);
            norms[i] = 1L;
        }

        scorer->scoreBatch(freqs, norms, scores);

        // Verify against classic BM25 formula (SIMD scorer uses (k1+1) factor)
        for (int i = 0; i < DIAGON_BM25_BATCH_SIZE; i++) {
            float expected = computeExpectedSIMDScore(freqs[i], norms[i]);
            EXPECT_TRUE(approxEqual(scores[i], expected))
                << "batch=" << batch << ", i=" << i << ", freq=" << freqs[i]
                << ", expected=" << expected << ", actual=" << scores[i];
        }
    }
}

#endif  // DIAGON_HAVE_AVX2 || DIAGON_HAVE_NEON

// ==================== Performance Hint Tests ====================

TEST_F(BM25ScorerSIMDTest, FactoryFunction) {
    // Test factory function creates scorer

    TestDummyWeight weight;
    auto scorer = createBM25Scorer(weight, nullptr, idf_, k1_, b_);

    EXPECT_NE(scorer, nullptr);
}
