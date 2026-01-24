// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/BM25ScorerSIMD.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/Weight.h"
#include "diagon/search/TermQuery.h"
#include "diagon/index/LeafReaderContext.h"

#include <benchmark/benchmark.h>
#include <random>
#include <vector>

using namespace diagon::search;

// ==================== SIMD Scoring Benchmarks ====================

/**
 * Benchmark: Scalar BM25 scoring using BM25Similarity
 * Baseline for comparison using actual API
 */
static void BM_BM25_Scalar(benchmark::State& state) {
    const int numDocs = state.range(0);

    // Generate random frequencies
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> freq_dist(1, 100);

    std::vector<int> freqs(numDocs);
    for (int i = 0; i < numDocs; i++) {
        freqs[i] = freq_dist(rng);
    }

    // BM25 parameters
    float idf = 2.5f;
    float k1 = 1.2f;
    float b = 0.75f;

    BM25Similarity similarity(k1, b);

    int64_t docsProcessed = 0;

    for (auto _ : state) {
        float totalScore = 0.0f;

        // Score all documents using BM25Similarity (prevents auto-vectorization)
        for (int i = 0; i < numDocs; i++) {
            float score = idf * similarity.score(static_cast<float>(freqs[i]), 1L);
            totalScore += score;
        }

        benchmark::DoNotOptimize(totalScore);
        docsProcessed += numDocs;
    }

    state.SetItemsProcessed(docsProcessed);
    state.SetLabel("scalar");
}

#ifdef DIAGON_HAVE_AVX2

/**
 * Benchmark: SIMD BM25 scoring
 * Vectorized implementation
 */
static void BM_BM25_SIMD(benchmark::State& state) {
    const int numDocs = state.range(0);

    // Generate random frequencies (aligned for SIMD)
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> freq_dist(1, 100);

    // Round up to multiple of 8 for SIMD
    int numDocsAligned = ((numDocs + 7) / 8) * 8;

    alignas(32) std::vector<int> freqs(numDocsAligned);
    alignas(32) std::vector<long> norms(numDocsAligned, 1L);
    alignas(32) std::vector<float> scores(numDocsAligned);

    for (int i = 0; i < numDocs; i++) {
        freqs[i] = freq_dist(rng);
    }
    // Pad remaining with zeros
    for (int i = numDocs; i < numDocsAligned; i++) {
        freqs[i] = 0;
    }

    // BM25 parameters
    float idf = 2.5f;
    float k1 = 1.2f;
    float b = 0.75f;

    // Create dummy weight
    class DummyWeight : public Weight {
    public:
        std::unique_ptr<Scorer> scorer(const diagon::index::LeafReaderContext&) const override {
            return nullptr;
        }
        const Query& getQuery() const override {
            static TermQuery dummyQuery(diagon::search::Term("", ""));
            return dummyQuery;
        }
    };

    DummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, std::unique_ptr<diagon::index::PostingsEnum>(nullptr), idf, k1, b);

    int64_t docsProcessed = 0;

    for (auto _ : state) {
        float totalScore = 0.0f;

        // Score all documents in batches of 8 (SIMD)
        for (int i = 0; i < numDocsAligned; i += 8) {
            scorer->scoreBatch(&freqs[i], &norms[i], &scores[i]);

            // Sum scores
            for (int j = 0; j < 8; j++) {
                totalScore += scores[i + j];
            }
        }

        benchmark::DoNotOptimize(totalScore);
        docsProcessed += numDocs;
    }

    state.SetItemsProcessed(docsProcessed);
    state.SetLabel("SIMD");
}

/**
 * Benchmark: SIMD BM25 scoring with uniform norm (optimization)
 */
static void BM_BM25_SIMDUniformNorm(benchmark::State& state) {
    const int numDocs = state.range(0);

    // Generate random frequencies
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> freq_dist(1, 100);

    int numDocsAligned = ((numDocs + 7) / 8) * 8;

    alignas(32) std::vector<int> freqs(numDocsAligned);
    alignas(32) std::vector<float> scores(numDocsAligned);

    for (int i = 0; i < numDocs; i++) {
        freqs[i] = freq_dist(rng);
    }
    for (int i = numDocs; i < numDocsAligned; i++) {
        freqs[i] = 0;
    }

    float idf = 2.5f;
    float k1 = 1.2f;
    float b = 0.75f;

    class DummyWeight : public Weight {
    public:
        std::unique_ptr<Scorer> scorer(const diagon::index::LeafReaderContext&) const override {
            return nullptr;
        }
        const Query& getQuery() const override {
            static TermQuery dummyQuery(diagon::search::Term("", ""));
            return dummyQuery;
        }
    };

    DummyWeight weight;
    auto scorer = std::make_unique<BM25ScorerSIMD>(weight, std::unique_ptr<diagon::index::PostingsEnum>(nullptr), idf, k1, b);

    int64_t docsProcessed = 0;

    for (auto _ : state) {
        float totalScore = 0.0f;

        // Score with uniform norm optimization
        for (int i = 0; i < numDocsAligned; i += 8) {
            scorer->scoreBatchUniformNorm(&freqs[i], 1L, &scores[i]);

            for (int j = 0; j < 8; j++) {
                totalScore += scores[i + j];
            }
        }

        benchmark::DoNotOptimize(totalScore);
        docsProcessed += numDocs;
    }

    state.SetItemsProcessed(docsProcessed);
    state.SetLabel("SIMD-uniform");
}

#endif  // DIAGON_HAVE_AVX2

// ==================== Benchmark Registrations ====================

// Scalar baseline
BENCHMARK(BM_BM25_Scalar)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMicrosecond);

#ifdef DIAGON_HAVE_AVX2

// SIMD version
BENCHMARK(BM_BM25_SIMD)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMicrosecond);

// SIMD with uniform norm optimization
BENCHMARK(BM_BM25_SIMDUniformNorm)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMicrosecond);

#endif  // DIAGON_HAVE_AVX2

BENCHMARK_MAIN();
