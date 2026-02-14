// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * LuceneComparisonBenchmark - Compare Diagon search performance with Lucene
 *
 * This benchmark creates a comparable workload to Lucene's benchmark suite
 * using similar document sizes, query types, and configurations.
 *
 * Metrics:
 * - Search latency (microseconds)
 * - Query throughput (QPS)
 * - Documents scanned per second
 */

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/search/BooleanClause.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <memory>
#include <random>
#include <vector>

using namespace diagon;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Test Data ====================

namespace {

// Sample vocabulary (100 common words)
const std::vector<std::string> VOCABULARY = {
    "the",    "be",    "to",   "of",      "and",   "a",     "in",    "that",  "have",  "i",
    "it",     "for",   "not",  "on",      "with",  "he",    "as",    "you",   "do",    "at",
    "this",   "but",   "his",  "by",      "from",  "they",  "we",    "say",   "her",   "she",
    "or",     "an",    "will", "my",      "one",   "all",   "would", "there", "their", "what",
    "so",     "up",    "out",  "if",      "about", "who",   "get",   "which", "go",    "me",
    "when",   "make",  "can",  "like",    "time",  "no",    "just",  "him",   "know",  "take",
    "people", "into",  "year", "your",    "good",  "some",  "could", "them",  "see",   "other",
    "than",   "then",  "now",  "look",    "only",  "come",  "its",   "over",  "think", "also",
    "back",   "after", "use",  "two",     "how",   "our",   "work",  "first", "well",  "way",
    "even",   "new",   "want", "because", "any",   "these", "give",  "day",   "most",  "us"};

/**
 * Generate synthetic document with random words
 */
std::string generateDocument(std::mt19937& rng, int numWords) {
    std::uniform_int_distribution<size_t> dist(0, VOCABULARY.size() - 1);

    std::string doc;
    for (int i = 0; i < numWords; i++) {
        if (i > 0)
            doc += " ";
        doc += VOCABULARY[dist(rng)];
    }
    return doc;
}

/**
 * Create test index
 */
class TestIndex {
public:
    TestIndex(int numDocs, int avgDocLength)
        : rng_(42) {
        // Create temp directory
        testDir_ = fs::temp_directory_path() / "diagon_lucene_comparison";
        fs::create_directories(testDir_);

        directory_ = FSDirectory::open(testDir_.string());

        // Create index
        DocumentsWriterPerThread::Config config;
        DocumentsWriterPerThread dwpt(config, directory_.get(), "Lucene104");

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            std::string text = generateDocument(rng_, avgDocLength);
            doc.add(std::make_unique<TextField>("content", text));
            dwpt.addDocument(doc);
        }

        segmentInfo_ = dwpt.flush();

        // Open SegmentReader directly (avoids need for segments_N file)
        segmentReader_ = SegmentReader::open(*directory_, segmentInfo_);
    }

    ~TestIndex() {
        segmentReader_.reset();
        directory_.reset();
        if (fs::exists(testDir_)) {
            fs::remove_all(testDir_);
        }
    }

    IndexSearcher createSearcher() {
        // IndexSearcher accepts IndexReader&
        // SegmentReader extends LeafReader extends IndexReader
        return IndexSearcher(*segmentReader_);
    }

    SegmentReader* getReader() { return segmentReader_.get(); }

private:
    fs::path testDir_;
    std::unique_ptr<Directory> directory_;
    std::shared_ptr<SegmentInfo> segmentInfo_;
    std::shared_ptr<SegmentReader> segmentReader_;
    std::mt19937 rng_;
};

// Global test index (created once, reused across benchmarks)
std::unique_ptr<TestIndex> g_testIndex;

void SetupTestIndex(int numDocs, int avgDocLength) {
    if (!g_testIndex) {
        g_testIndex = std::make_unique<TestIndex>(numDocs, avgDocLength);
    }
}

}  // anonymous namespace

// ==================== Search Benchmarks ====================

/**
 * Benchmark: Single term query (common term)
 * Comparable to Lucene's TermQuery benchmark
 */
static void BM_Search_TermQuery_Common(benchmark::State& state) {
    SetupTestIndex(10000, 100);
    auto searcher = g_testIndex->createSearcher();

    // Query for common term "the"
    search::Term term("content", "the");
    TermQuery query(term);

    for (auto _ : state) {
        TopDocs results = searcher.search(query, 10);
        benchmark::DoNotOptimize(results);
    }

    // Report queries per second
    state.SetItemsProcessed(state.iterations());
    state.counters["QPS"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

/**
 * Benchmark: Single term query (rare term)
 */
static void BM_Search_TermQuery_Rare(benchmark::State& state) {
    SetupTestIndex(10000, 100);
    auto searcher = g_testIndex->createSearcher();

    // Query for rare term "because"
    search::Term term("content", "because");
    TermQuery query(term);

    for (auto _ : state) {
        TopDocs results = searcher.search(query, 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["QPS"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

/**
 * Benchmark: Boolean query with 2 terms (AND)
 */
static void BM_Search_BooleanAND(benchmark::State& state) {
    SetupTestIndex(10000, 100);
    auto searcher = g_testIndex->createSearcher();

    // Query: "the" AND "and"
    auto term1 = std::make_unique<TermQuery>(search::Term("content", "the"));
    auto term2 = std::make_unique<TermQuery>(search::Term("content", "and"));

    BooleanQuery::Builder builder;
    builder.add(std::move(term1), Occur::MUST);
    builder.add(std::move(term2), Occur::MUST);
    auto query = builder.build();

    for (auto _ : state) {
        TopDocs results = searcher.search(*query, 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["QPS"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

/**
 * Benchmark: Boolean query with 2 terms (OR)
 */
static void BM_Search_BooleanOR(benchmark::State& state) {
    SetupTestIndex(10000, 100);
    auto searcher = g_testIndex->createSearcher();

    // Query: "people" OR "time"
    auto term1 = std::make_unique<TermQuery>(search::Term("content", "people"));
    auto term2 = std::make_unique<TermQuery>(search::Term("content", "time"));

    BooleanQuery::Builder builder;
    builder.add(std::move(term1), Occur::SHOULD);
    builder.add(std::move(term2), Occur::SHOULD);
    auto query = builder.build();

    for (auto _ : state) {
        TopDocs results = searcher.search(*query, 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["QPS"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

/**
 * Benchmark: Boolean query with 3 terms (complex)
 */
static void BM_Search_Boolean3Terms(benchmark::State& state) {
    SetupTestIndex(10000, 100);
    auto searcher = g_testIndex->createSearcher();

    // Query: "the" AND ("people" OR "time")
    auto term1 = std::make_unique<TermQuery>(search::Term("content", "the"));
    auto term2 = std::make_unique<TermQuery>(search::Term("content", "people"));
    auto term3 = std::make_unique<TermQuery>(search::Term("content", "time"));

    BooleanQuery::Builder builder;
    builder.add(std::move(term1), Occur::MUST);
    builder.add(std::move(term2), Occur::SHOULD);
    builder.add(std::move(term3), Occur::SHOULD);
    auto query = builder.build();

    for (auto _ : state) {
        TopDocs results = searcher.search(*query, 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["QPS"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

/**
 * Benchmark: TopK variation (different result set sizes)
 */
static void BM_Search_TopK(benchmark::State& state) {
    SetupTestIndex(10000, 100);
    auto searcher = g_testIndex->createSearcher();

    int topK = state.range(0);

    search::Term term("content", "the");
    TermQuery query(term);

    for (auto _ : state) {
        TopDocs results = searcher.search(query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["QPS"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

// Register benchmarks
BENCHMARK(BM_Search_TermQuery_Common)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Search_TermQuery_Rare)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Search_BooleanAND)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Search_BooleanOR)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Search_Boolean3Terms)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Search_TopK)->Arg(10)->Arg(50)->Arg(100)->Arg(1000)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
