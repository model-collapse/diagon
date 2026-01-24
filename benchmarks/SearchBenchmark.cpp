// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>
#include <sstream>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Test Corpus Setup ====================

/**
 * Generate random text for document
 */
std::string generateRandomText(int numWords, std::mt19937& rng) {
    static const std::vector<std::string> words = {
        "search",
        "engine",
        "index",
        "document",
        "query",
        "result",
        "score",
        "lucene",
        "elasticsearch",
        "database",
        "algorithm",
        "data",
        "fast",
        "performance",
        "benchmark",
        "optimization",
        "memory",
        "distributed",
        "the",
        "quick",
        "brown",
        "fox",
        "jumps",
        "over",
        "lazy",
        "dog"
    };

    std::uniform_int_distribution<> dist(0, words.size() - 1);
    std::ostringstream oss;

    for (int i = 0; i < numWords; i++) {
        if (i > 0) oss << " ";
        oss << words[dist(rng)];
    }

    return oss.str();
}

/**
 * Create test index with specified number of documents
 */
std::unique_ptr<FSDirectory> createTestIndex(int numDocs, const fs::path& indexPath) {
    fs::create_directories(indexPath);
    auto dir = FSDirectory::open(indexPath.string());

    IndexWriterConfig config;
    config.setRAMBufferSizeMB(32.0);
    IndexWriter writer(*dir, config);

    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    ft.stored = true;
    ft.tokenized = true;

    std::mt19937 rng(12345);  // Fixed seed

    for (int i = 0; i < numDocs; i++) {
        Document doc;
        std::string text = generateRandomText(50, rng);
        doc.add(std::make_unique<Field>("body", text, ft));
        writer.addDocument(doc);
    }

    writer.commit();
    return dir;
}

// ==================== Search Benchmarks ====================

/**
 * Benchmark: Basic term query search
 * Measures queries per second for simple term queries
 */
static void BM_TermQuerySearch(benchmark::State& state) {
    const int numDocs = state.range(0);
    const int topK = 10;

    // Setup: Create index once
    static std::unordered_map<int, std::unique_ptr<FSDirectory>> indexCache;
    static std::unordered_map<int, fs::path> pathCache;

    if (indexCache.find(numDocs) == indexCache.end()) {
        fs::path indexPath = fs::temp_directory_path() / ("diagon_search_bench_" + std::to_string(numDocs));
        indexCache[numDocs] = createTestIndex(numDocs, indexPath);
        pathCache[numDocs] = indexPath;
    }

    auto& dir = indexCache[numDocs];
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Create query
    search::Term term("_all", "search");  // Common term
    TermQuery query(term);

    for (auto _ : state) {
        auto results = searcher.search(query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(std::to_string(numDocs) + " docs");
    // Note: Cleanup happens at process exit
}

/**
 * Benchmark: Different result set sizes (topK)
 * Measures impact of collecting more results
 */
static void BM_SearchWithDifferentTopK(benchmark::State& state) {
    const int numDocs = 10000;
    const int topK = state.range(0);

    static std::unique_ptr<FSDirectory> dir;
    static fs::path indexPath;
    static bool initialized = false;

    if (!initialized) {
        indexPath = fs::temp_directory_path() / "diagon_topk_bench";
        dir = createTestIndex(numDocs, indexPath);
        initialized = true;
    }

    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    search::Term term("_all", "search");
    TermQuery query(term);

    for (auto _ : state) {
        auto results = searcher.search(query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("top-" + std::to_string(topK));
    // Note: Cleanup happens at process exit
}

/**
 * Benchmark: Query frequency impact
 * Measures performance for rare vs common terms
 */
static void BM_SearchRareVsCommonTerms(benchmark::State& state) {
    const int numDocs = 10000;
    const int topK = 10;
    const bool rareQuery = (state.range(0) == 0);

    static std::unique_ptr<FSDirectory> dir;
    static fs::path indexPath;
    static bool initialized = false;

    if (!initialized) {
        indexPath = fs::temp_directory_path() / "diagon_freq_bench";
        dir = createTestIndex(numDocs, indexPath);
        initialized = true;
    }

    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Rare term appears in ~1% of docs, common term in ~50%
    search::Term term("_all", rareQuery ? "elasticsearch" : "the");
    TermQuery query(term);

    for (auto _ : state) {
        auto results = searcher.search(query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(rareQuery ? "rare term" : "common term");
    // Note: Cleanup happens at process exit
}

/**
 * Benchmark: Reader reuse
 * Measures cost of opening reader vs reusing
 */
static void BM_ReaderReuse(benchmark::State& state) {
    const int numDocs = 5000;
    const int topK = 10;
    const bool reuseReader = (state.range(0) == 1);

    static std::unique_ptr<FSDirectory> dir;
    static fs::path indexPath;
    static bool initialized = false;

    if (!initialized) {
        indexPath = fs::temp_directory_path() / "diagon_reader_bench";
        dir = createTestIndex(numDocs, indexPath);
        initialized = true;
    }

    search::Term term("_all", "search");
    TermQuery query(term);

    if (reuseReader) {
        // Reuse reader across iterations
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        for (auto _ : state) {
            auto results = searcher.search(query, topK);
            benchmark::DoNotOptimize(results);
        }
    } else {
        // Open new reader each iteration
        for (auto _ : state) {
            auto reader = DirectoryReader::open(*dir);
            IndexSearcher searcher(*reader);
            auto results = searcher.search(query, topK);
            benchmark::DoNotOptimize(results);
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(reuseReader ? "reuse reader" : "new reader");
    // Note: Cleanup happens at process exit
}

/**
 * Benchmark: Count queries (no scoring)
 * Measures count() performance vs full search
 */
static void BM_CountVsSearch(benchmark::State& state) {
    const int numDocs = 10000;
    const bool useCount = (state.range(0) == 1);

    static std::unique_ptr<FSDirectory> dir;
    static fs::path indexPath;
    static bool initialized = false;

    if (!initialized) {
        indexPath = fs::temp_directory_path() / "diagon_count_bench";
        dir = createTestIndex(numDocs, indexPath);
        initialized = true;
    }

    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    search::Term term("_all", "search");
    TermQuery query(term);

    if (useCount) {
        for (auto _ : state) {
            int count = searcher.count(query);
            benchmark::DoNotOptimize(count);
        }
    } else {
        for (auto _ : state) {
            auto results = searcher.search(query, Integer::MAX_VALUE);
            benchmark::DoNotOptimize(results);
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(useCount ? "count()" : "search(all)");
    // Note: Cleanup happens at process exit
}

// ==================== Benchmark Registrations ====================

// Basic term query with different index sizes
BENCHMARK(BM_TermQuerySearch)
    ->Arg(1000)
    ->Arg(5000)
    ->Arg(10000)
    ->Arg(50000)
    ->Unit(benchmark::kMicrosecond);

// Different topK sizes
BENCHMARK(BM_SearchWithDifferentTopK)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

// Rare vs common terms
BENCHMARK(BM_SearchRareVsCommonTerms)
    ->Arg(0)  // rare
    ->Arg(1)  // common
    ->Unit(benchmark::kMicrosecond);

// Reader reuse
BENCHMARK(BM_ReaderReuse)
    ->Arg(0)  // new reader
    ->Arg(1)  // reuse reader
    ->Unit(benchmark::kMicrosecond);

// Count vs full search
BENCHMARK(BM_CountVsSearch)
    ->Arg(0)  // search all
    ->Arg(1)  // count
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
