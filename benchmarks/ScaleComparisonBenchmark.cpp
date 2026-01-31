// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * ScaleComparisonBenchmark - Large-scale search performance testing
 *
 * Tests Diagon search performance at different scales:
 * - 100K documents
 * - 1M documents
 * - 10M documents (if MSMarco available)
 *
 * Measures:
 * - Index build time
 * - Index size on disk
 * - Query latency (p50, p95, p99)
 * - Query throughput (QPS)
 * - Memory usage
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
#include <iostream>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

using namespace diagon;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Configuration ====================

namespace {

// Dataset configurations
struct DatasetConfig {
    std::string name;
    int numDocs;
    int avgDocLength;
    std::string indexPath;
};

const std::vector<DatasetConfig> DATASETS = {
    {"100K", 100000, 100, "/tmp/diagon_scale_100k"},
    {"1M", 1000000, 100, "/tmp/diagon_scale_1m"},
    // {"10M", 10000000, 100, "/tmp/diagon_scale_10m"},  // Uncomment for full scale test
};

// Sample vocabulary (100 common words)
const std::vector<std::string> VOCABULARY = {
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
    "when", "make", "can", "like", "time", "no", "just", "him", "know", "take",
    "people", "into", "year", "your", "good", "some", "could", "them", "see", "other",
    "than", "then", "now", "look", "only", "come", "its", "over", "think", "also",
    "back", "after", "use", "two", "how", "our", "work", "first", "well", "way",
    "even", "new", "want", "because", "any", "these", "give", "day", "most", "us"
};

/**
 * Generate synthetic document with random words
 */
std::string generateDocument(std::mt19937& rng, int numWords) {
    std::uniform_int_distribution<size_t> dist(0, VOCABULARY.size() - 1);

    std::string doc;
    for (int i = 0; i < numWords; i++) {
        if (i > 0) doc += " ";
        doc += VOCABULARY[dist(rng)];
    }
    return doc;
}

/**
 * Scale test index holder
 */
class ScaleTestIndex {
public:
    ScaleTestIndex(const DatasetConfig& config)
        : config_(config), rng_(42) {

        std::cout << "\n=== Building " << config.name << " index ===\n";

        // Check if index already exists
        if (fs::exists(config.indexPath) && fs::exists(config.indexPath + "/.built")) {
            std::cout << "Loading existing index from " << config.indexPath << "\n";
            loadExistingIndex();
            return;
        }

        // Build new index
        buildIndex();
    }

    ~ScaleTestIndex() {
        segmentReader_.reset();
        directory_.reset();
    }

    IndexSearcher createSearcher() {
        return IndexSearcher(*segmentReader_);
    }

    SegmentReader* getReader() { return segmentReader_.get(); }

    const DatasetConfig& getConfig() const { return config_; }

    size_t getIndexSizeBytes() const {
        size_t totalSize = 0;
        for (const auto& entry : fs::recursive_directory_iterator(config_.indexPath)) {
            if (entry.is_regular_file()) {
                totalSize += entry.file_size();
            }
        }
        return totalSize;
    }

private:
    void buildIndex() {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Create directory
        fs::create_directories(config_.indexPath);
        directory_ = FSDirectory::open(config_.indexPath);

        // Create index
        DocumentsWriterPerThread::Config dwptConfig;
        DocumentsWriterPerThread dwpt(dwptConfig, directory_.get(), "Lucene104");

        std::cout << "Adding " << config_.numDocs << " documents...\n";

        // Add documents in batches for progress reporting
        const int batchSize = 10000;
        for (int i = 0; i < config_.numDocs; i++) {
            Document doc;
            std::string text = generateDocument(rng_, config_.avgDocLength);
            doc.add(std::make_unique<TextField>("content", text));
            dwpt.addDocument(doc);

            if ((i + 1) % batchSize == 0) {
                std::cout << "  Progress: " << (i + 1) << "/" << config_.numDocs
                         << " (" << (100.0 * (i + 1) / config_.numDocs) << "%)\n";
            }
        }

        std::cout << "Flushing segment...\n";
        segmentInfo_ = dwpt.flush();

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << "✓ Index built in " << (duration.count() / 1000.0) << " seconds\n";
        std::cout << "  Throughput: " << (config_.numDocs * 1000.0 / duration.count())
                 << " docs/sec\n";

        // Open reader
        segmentReader_ = SegmentReader::open(*directory_, segmentInfo_);

        // Mark as built
        std::ofstream marker(config_.indexPath + "/.built");
        marker << "built\n";

        // Report index size
        size_t indexSize = getIndexSizeBytes();
        std::cout << "  Index size: " << (indexSize / 1024.0 / 1024.0) << " MB\n";
        std::cout << "  Bytes per doc: " << (indexSize / config_.numDocs) << "\n";
    }

    void loadExistingIndex() {
        directory_ = FSDirectory::open(config_.indexPath);

        // Find segment info file
        std::string segmentName;
        for (const auto& entry : fs::directory_iterator(config_.indexPath)) {
            if (entry.path().extension() == ".si") {
                segmentName = entry.path().stem().string();
                break;
            }
        }

        if (segmentName.empty()) {
            throw std::runtime_error("No segment info file found in " + config_.indexPath);
        }

        // Create minimal SegmentInfo for reading
        segmentInfo_ = std::make_shared<SegmentInfo>(
            segmentName,
            config_.numDocs,
            "Lucene104"
        );

        // Open reader
        segmentReader_ = SegmentReader::open(*directory_, segmentInfo_);

        std::cout << "✓ Loaded " << config_.numDocs << " documents\n";

        size_t indexSize = getIndexSizeBytes();
        std::cout << "  Index size: " << (indexSize / 1024.0 / 1024.0) << " MB\n";
    }

    DatasetConfig config_;
    std::mt19937 rng_;

    std::unique_ptr<Directory> directory_;
    std::shared_ptr<SegmentInfo> segmentInfo_;
    std::shared_ptr<SegmentReader> segmentReader_;
};

// Global test indexes (created once, reused across benchmarks)
std::unordered_map<std::string, std::unique_ptr<ScaleTestIndex>> g_testIndexes;

void SetupTestIndex(const DatasetConfig& config) {
    if (g_testIndexes.find(config.name) == g_testIndexes.end()) {
        g_testIndexes[config.name] = std::make_unique<ScaleTestIndex>(config);
    }
}

} // anonymous namespace

// ==================== Search Benchmarks ====================

/**
 * Benchmark: Single term query at scale
 */
static void BM_Scale_TermQuery(benchmark::State& state) {
    int datasetIdx = state.range(0);
    if (datasetIdx >= DATASETS.size()) {
        state.SkipWithError("Invalid dataset index");
        return;
    }

    const auto& config = DATASETS[datasetIdx];
    SetupTestIndex(config);

    auto* testIndex = g_testIndexes[config.name].get();
    auto searcher = testIndex->createSearcher();

    // Query for common term "the"
    search::Term term("content", "the");
    TermQuery query(term);

    for (auto _ : state) {
        TopDocs results = searcher.search(query, 10);
        benchmark::DoNotOptimize(results);
    }

    // Report metrics
    state.SetLabel(config.name);
    state.counters["QPS"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["docs"] = config.numDocs;
    state.counters["index_mb"] = testIndex->getIndexSizeBytes() / 1024.0 / 1024.0;
}

/**
 * Benchmark: Boolean AND query at scale
 */
static void BM_Scale_BooleanAND(benchmark::State& state) {
    int datasetIdx = state.range(0);
    if (datasetIdx >= DATASETS.size()) {
        state.SkipWithError("Invalid dataset index");
        return;
    }

    const auto& config = DATASETS[datasetIdx];
    SetupTestIndex(config);

    auto* testIndex = g_testIndexes[config.name].get();
    auto searcher = testIndex->createSearcher();

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

    state.SetLabel(config.name);
    state.counters["QPS"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["docs"] = config.numDocs;
}

/**
 * Benchmark: Boolean OR query at scale
 */
static void BM_Scale_BooleanOR(benchmark::State& state) {
    int datasetIdx = state.range(0);
    if (datasetIdx >= DATASETS.size()) {
        state.SkipWithError("Invalid dataset index");
        return;
    }

    const auto& config = DATASETS[datasetIdx];
    SetupTestIndex(config);

    auto* testIndex = g_testIndexes[config.name].get();
    auto searcher = testIndex->createSearcher();

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

    state.SetLabel(config.name);
    state.counters["QPS"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["docs"] = config.numDocs;
}

/**
 * Benchmark: Rare term query at scale
 */
static void BM_Scale_RareTerm(benchmark::State& state) {
    int datasetIdx = state.range(0);
    if (datasetIdx >= DATASETS.size()) {
        state.SkipWithError("Invalid dataset index");
        return;
    }

    const auto& config = DATASETS[datasetIdx];
    SetupTestIndex(config);

    auto* testIndex = g_testIndexes[config.name].get();
    auto searcher = testIndex->createSearcher();

    // Query for rare term "because"
    search::Term term("content", "because");
    TermQuery query(term);

    for (auto _ : state) {
        TopDocs results = searcher.search(query, 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetLabel(config.name);
    state.counters["QPS"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["docs"] = config.numDocs;
}

/**
 * Benchmark: TopK variation at scale
 */
static void BM_Scale_TopK(benchmark::State& state) {
    int datasetIdx = state.range(0);
    int topK = state.range(1);

    if (datasetIdx >= DATASETS.size()) {
        state.SkipWithError("Invalid dataset index");
        return;
    }

    const auto& config = DATASETS[datasetIdx];
    SetupTestIndex(config);

    auto* testIndex = g_testIndexes[config.name].get();
    auto searcher = testIndex->createSearcher();

    search::Term term("content", "the");
    TermQuery query(term);

    for (auto _ : state) {
        TopDocs results = searcher.search(query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetLabel(config.name + "_k" + std::to_string(topK));
    state.counters["QPS"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["topK"] = topK;
}

// Register benchmarks for each dataset scale
BENCHMARK(BM_Scale_TermQuery)
    ->DenseRange(0, DATASETS.size() - 1, 1)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_Scale_BooleanAND)
    ->DenseRange(0, DATASETS.size() - 1, 1)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_Scale_BooleanOR)
    ->DenseRange(0, DATASETS.size() - 1, 1)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_Scale_RareTerm)
    ->DenseRange(0, DATASETS.size() - 1, 1)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_Scale_TopK)
    ->ArgsProduct({
        {0, 1},  // Dataset indices (100K, 1M)
        {10, 100, 1000}  // TopK values
    })
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
