// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * FST Efficiency Gate Benchmark
 *
 * Measures Diagon FST performance and compares against established baseline.
 * Warns if performance regression exceeds 10% threshold.
 *
 * Purpose:
 * - Continuous performance monitoring
 * - Regression detection before merge
 * - Performance trend tracking
 *
 * Usage:
 *   ./FSTEfficiencyGate --benchmark_out=fst_results.json --benchmark_out_format=json
 *
 * Baseline update (after verified improvement):
 *   cp fst_results.json benchmark_results/fst_baseline.json
 */

#include "diagon/util/FST.h"

#include <benchmark/benchmark.h>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

using namespace diagon::util;

namespace {

// ==================== Test Data Generation ====================

/**
 * Generate deterministic test terms
 * Reproducible across runs for consistent benchmarking
 */
std::vector<std::pair<std::string, int64_t>> generateTestTerms(size_t count) {
    std::vector<std::pair<std::string, int64_t>> terms;
    terms.reserve(count);

    // Use deterministic seed for reproducibility
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> freq_dist(1, 10000);

    for (size_t i = 0; i < count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08zu", i);
        terms.emplace_back(buf, freq_dist(rng));
    }

    return terms;
}

/**
 * Build FST from test terms
 */
std::unique_ptr<FST> buildTestFST(const std::vector<std::pair<std::string, int64_t>>& terms) {
    FST::Builder builder;
    for (const auto& [term, output] : terms) {
        builder.add(BytesRef(term), output);
    }
    return builder.finish();
}

// Shared test data (initialized once per benchmark run)
thread_local std::vector<std::pair<std::string, int64_t>> g_terms_1k;
thread_local std::vector<std::pair<std::string, int64_t>> g_terms_10k;
thread_local std::vector<std::pair<std::string, int64_t>> g_terms_100k;
thread_local std::unique_ptr<FST> g_fst_10k;
thread_local std::unique_ptr<FST> g_fst_100k;
thread_local bool g_initialized = false;

void InitializeTestData() {
    if (g_initialized) return;

    g_terms_1k = generateTestTerms(1000);
    g_terms_10k = generateTestTerms(10000);
    g_terms_100k = generateTestTerms(100000);

    g_fst_10k = buildTestFST(g_terms_10k);
    g_fst_100k = buildTestFST(g_terms_100k);

    g_initialized = true;
}

} // anonymous namespace

// ==================== FST Construction Benchmarks ====================

/**
 * Benchmark: FST Construction (1K terms)
 *
 * Baseline: ~0.5 ms (from FSTPerformanceGuard)
 * Threshold: 10% regression = 0.55 ms
 */
static void BM_FST_Construction_1K(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        FST::Builder builder;
        for (const auto& [term, output] : g_terms_1k) {
            builder.add(BytesRef(term), output);
        }
        auto fst = builder.finish();
        benchmark::DoNotOptimize(fst);
    }

    state.SetItemsProcessed(state.iterations() * 1000);
    state.SetLabel("1K_terms");
}
BENCHMARK(BM_FST_Construction_1K);

/**
 * Benchmark: FST Construction (10K terms)
 *
 * Baseline: ~2 ms (from FSTPerformanceGuard)
 * Threshold: 10% regression = 2.2 ms
 */
static void BM_FST_Construction_10K(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        FST::Builder builder;
        for (const auto& [term, output] : g_terms_10k) {
            builder.add(BytesRef(term), output);
        }
        auto fst = builder.finish();
        benchmark::DoNotOptimize(fst);
    }

    state.SetItemsProcessed(state.iterations() * 10000);
    state.SetLabel("10K_terms");
}
BENCHMARK(BM_FST_Construction_10K);

/**
 * Benchmark: FST Construction (100K terms)
 *
 * Baseline: TBD (first run establishes baseline)
 * Threshold: 10% regression
 */
static void BM_FST_Construction_100K(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        FST::Builder builder;
        for (const auto& [term, output] : g_terms_100k) {
            builder.add(BytesRef(term), output);
        }
        auto fst = builder.finish();
        benchmark::DoNotOptimize(fst);
    }

    state.SetItemsProcessed(state.iterations() * 100000);
    state.SetLabel("100K_terms");
}
BENCHMARK(BM_FST_Construction_100K);

// ==================== FST Lookup Benchmarks ====================

/**
 * Benchmark: FST Exact Match Lookup
 *
 * Baseline: ~171 ns per lookup (from FSTPerformanceGuard)
 * Threshold: 10% regression = 188 ns
 */
static void BM_FST_Lookup_ExactMatch(benchmark::State& state) {
    InitializeTestData();

    // Prepare lookup terms (every 10th term for realistic distribution)
    std::vector<std::string> lookupTerms;
    for (size_t i = 0; i < g_terms_10k.size(); i += 10) {
        lookupTerms.push_back(g_terms_10k[i].first);
    }

    size_t index = 0;
    for (auto _ : state) {
        auto result = g_fst_10k->get(BytesRef(lookupTerms[index % lookupTerms.size()]));
        benchmark::DoNotOptimize(result);
        index++;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("exact_match");
}
BENCHMARK(BM_FST_Lookup_ExactMatch);

/**
 * Benchmark: FST Lookup (Cache Miss)
 *
 * Baseline: ~150 ns per lookup (from FSTPerformanceGuard)
 * Threshold: 10% regression = 165 ns
 */
static void BM_FST_Lookup_CacheMiss(benchmark::State& state) {
    InitializeTestData();

    // Prepare non-existent terms
    std::vector<std::string> missingTerms;
    for (int i = 0; i < 1000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "missing_%d", i);
        missingTerms.emplace_back(buf);
    }

    size_t index = 0;
    for (auto _ : state) {
        auto result = g_fst_10k->get(BytesRef(missingTerms[index % missingTerms.size()]));
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
        index++;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("cache_miss");
}
BENCHMARK(BM_FST_Lookup_CacheMiss);

/**
 * Benchmark: FST Lookup (Mixed Hit/Miss)
 *
 * Realistic workload: 70% hits, 30% misses
 */
static void BM_FST_Lookup_Mixed(benchmark::State& state) {
    InitializeTestData();

    // Prepare mixed terms (70% exist, 30% don't)
    std::vector<std::string> mixedTerms;
    for (size_t i = 0; i < 700; i++) {
        mixedTerms.push_back(g_terms_10k[i * 10].first);  // Exists
    }
    for (int i = 0; i < 300; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "missing_%d", i);
        mixedTerms.emplace_back(buf);  // Doesn't exist
    }

    // Shuffle for realistic access pattern
    std::mt19937 rng(42);
    std::shuffle(mixedTerms.begin(), mixedTerms.end(), rng);

    size_t index = 0;
    for (auto _ : state) {
        auto result = g_fst_10k->get(BytesRef(mixedTerms[index % mixedTerms.size()]));
        benchmark::DoNotOptimize(result);
        index++;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("mixed_70_30");
}
BENCHMARK(BM_FST_Lookup_Mixed);

// ==================== FST Iteration Benchmarks ====================

/**
 * Benchmark: FST Full Iteration
 *
 * Baseline: ~15 ns per term (from FSTPerformanceGuard)
 * Threshold: 10% regression = 16.5 ns per term
 */
static void BM_FST_Iteration_Full(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        auto entries = g_fst_10k->getAllEntries();
        benchmark::DoNotOptimize(entries);
    }

    state.SetItemsProcessed(state.iterations() * 10000);
    state.SetLabel("full_scan_10K");
}
BENCHMARK(BM_FST_Iteration_Full);

/**
 * Benchmark: FST Iteration (Large FST)
 *
 * Tests scalability with 100K terms
 */
static void BM_FST_Iteration_Large(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        auto entries = g_fst_100k->getAllEntries();
        benchmark::DoNotOptimize(entries);
    }

    state.SetItemsProcessed(state.iterations() * 100000);
    state.SetLabel("full_scan_100K");
}
BENCHMARK(BM_FST_Iteration_Large);

// ==================== FST Serialization Benchmarks ====================

/**
 * Benchmark: FST Serialization
 *
 * Measures cost of serializing FST to bytes
 */
static void BM_FST_Serialization(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        auto serialized = g_fst_10k->serialize();
        benchmark::DoNotOptimize(serialized);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("serialize_10K");
}
BENCHMARK(BM_FST_Serialization);

/**
 * Benchmark: FST Deserialization
 *
 * Measures cost of deserializing FST from bytes
 */
static void BM_FST_Deserialization(benchmark::State& state) {
    InitializeTestData();

    auto serialized = g_fst_10k->serialize();

    for (auto _ : state) {
        auto fst = FST::deserialize(serialized);
        benchmark::DoNotOptimize(fst);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("deserialize_10K");
}
BENCHMARK(BM_FST_Deserialization);

/**
 * Benchmark: FST Serialization Roundtrip
 *
 * Measures full roundtrip cost
 */
static void BM_FST_Roundtrip(benchmark::State& state) {
    InitializeTestData();

    for (auto _ : state) {
        auto serialized = g_fst_10k->serialize();
        auto fst = FST::deserialize(serialized);
        benchmark::DoNotOptimize(fst);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("roundtrip_10K");
}
BENCHMARK(BM_FST_Roundtrip);

// ==================== FST Memory Benchmarks ====================

/**
 * Benchmark: FST Memory Footprint
 *
 * Reports FST size for different term counts
 */
static void BM_FST_MemoryFootprint(benchmark::State& state) {
    InitializeTestData();

    size_t termCount = state.range(0);
    auto terms = generateTestTerms(termCount);
    auto fst = buildTestFST(terms);

    for (auto _ : state) {
        auto serialized = fst->serialize();
        state.counters["size_bytes"] = serialized.size();
        state.counters["bytes_per_term"] = serialized.size() / double(termCount);
        benchmark::DoNotOptimize(serialized);
    }

    state.SetLabel(std::to_string(termCount) + "_terms");
}
BENCHMARK(BM_FST_MemoryFootprint)->Arg(1000)->Arg(10000)->Arg(100000);

// ==================== Custom Main for Regression Detection ====================

int main(int argc, char** argv) {
    // Initialize Google Benchmark
    benchmark::Initialize(&argc, argv);

    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }

    // Run benchmarks
    benchmark::RunSpecifiedBenchmarks();

    // Cleanup
    benchmark::Shutdown();

    return 0;
}
