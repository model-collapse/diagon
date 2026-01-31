// Copyright 2024 Diagon Project
// Benchmark tokenizer performance

#include "diagon/util/FastTokenizer.h"

#include <benchmark/benchmark.h>
#include <sstream>
#include <string>
#include <vector>

using namespace diagon::util;

// Generate test text
static std::string generateTestText(int numWords) {
    static const std::vector<std::string> words = {
        "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
        "search", "engine", "index", "document", "query", "result", "score"
    };

    std::ostringstream oss;
    for (int i = 0; i < numWords; i++) {
        if (i > 0) oss << " ";
        oss << words[i % words.size()];
    }
    return oss.str();
}

// OLD: std::istringstream tokenization (baseline)
static std::vector<std::string> tokenizeOld(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// NEW: FastTokenizer with string_view
static void BM_FastTokenizer(benchmark::State& state) {
    const int numWords = state.range(0);
    std::string text = generateTestText(numWords);

    for (auto _ : state) {
        auto tokens = FastTokenizer::tokenize(text);
        benchmark::DoNotOptimize(tokens);
    }

    state.SetItemsProcessed(state.iterations() * numWords);
}

// OLD: std::istringstream baseline
static void BM_IStringStreamTokenizer(benchmark::State& state) {
    const int numWords = state.range(0);
    std::string text = generateTestText(numWords);

    for (auto _ : state) {
        auto tokens = tokenizeOld(text);
        benchmark::DoNotOptimize(tokens);
    }

    state.SetItemsProcessed(state.iterations() * numWords);
}

// Register benchmarks with different text sizes
BENCHMARK(BM_FastTokenizer)
    ->Arg(10)     // 10 words
    ->Arg(50)     // 50 words (typical doc)
    ->Arg(100)    // 100 words
    ->Arg(500)    // 500 words (large doc)
    ->Arg(1000);  // 1000 words

BENCHMARK(BM_IStringStreamTokenizer)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000);

BENCHMARK_MAIN();
