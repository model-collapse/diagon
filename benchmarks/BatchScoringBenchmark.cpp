// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/Query.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/TopScoreDocCollector.h"
#include "diagon/store/FSDirectory.h"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <random>

using namespace diagon;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::store;

namespace {

// Generate test documents
std::vector<Document> generateDocuments(int num_docs, int words_per_doc) {
    std::vector<Document> docs;
    docs.reserve(num_docs);

    // Vocabulary for test
    const std::vector<std::string> vocab = {
        "search",    "engine",        "index", "query",     "document", "term",      "score",
        "lucene",    "elasticsearch", "solr",  "algorithm", "data",     "structure", "performance",
        "benchmark", "optimization",  "cache", "memory",    "disk"};

    std::mt19937 rng(42);
    std::uniform_int_distribution<> word_dist(0, vocab.size() - 1);

    // Setup field type
    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS_AND_FREQS;
    ft.stored = true;
    ft.tokenized = true;

    for (int i = 0; i < num_docs; i++) {
        Document doc;

        // Generate document text
        std::string text;
        for (int j = 0; j < words_per_doc; j++) {
            if (j > 0)
                text += " ";
            text += vocab[word_dist(rng)];
        }

        doc.add(std::make_unique<Field>("body", text, ft));

        docs.push_back(std::move(doc));
    }

    return docs;
}

// Create index for testing - returns directory only
std::unique_ptr<FSDirectory> createTestIndex(const std::string& path, int num_docs) {
    // Clean up existing directory
    if (std::filesystem::exists(path)) {
        std::filesystem::remove_all(path);
    }
    std::filesystem::create_directories(path);

    auto directory = FSDirectory::open(path);

    // Index documents
    IndexWriterConfig config;
    config.setRAMBufferSizeMB(32.0);
    IndexWriter writer(*directory, config);
    auto docs = generateDocuments(num_docs, 50);  // 50 words per doc

    for (auto& doc : docs) {
        writer.addDocument(doc);
    }

    writer.commit();  // Important: commit, not close!

    return directory;
}

}  // namespace

// ==================== Benchmarks ====================

/**
 * Benchmark one-at-a-time scoring (baseline)
 */
static void BM_Search_OneAtATime(benchmark::State& state) {
    int num_docs = state.range(0);

    // Create test index
    auto directory = createTestIndex("/tmp/diagon_batch_bench_baseline", num_docs);

    // Open reader
    auto reader = DirectoryReader::open(*directory);

    // Create searcher with default config (one-at-a-time)
    IndexSearcher searcher(*reader);

    // Create query (search for common term "search")
    TermQuery query(search::Term("body", "search"));

    // Benchmark
    for (auto _ : state) {
        auto collector = TopScoreDocCollector::create(10);
        searcher.search(query, collector.get());
        auto results = collector->topDocs();
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
}

/**
 * Benchmark batch-at-a-time scoring (P1 optimization)
 */
static void BM_Search_BatchAtATime(benchmark::State& state) {
    int num_docs = state.range(0);

    // Create test index
    auto directory = createTestIndex("/tmp/diagon_batch_bench_optimized", num_docs);

    // Open reader
    auto reader = DirectoryReader::open(*directory);

    // Create searcher with batch mode enabled
    IndexSearcherConfig config;
    config.enable_batch_scoring = true;
    config.batch_size = 8;  // AVX2
    IndexSearcher searcher(*reader, config);

    // Create query (search for common term "search")
    TermQuery query(search::Term("body", "search"));

    // Benchmark
    for (auto _ : state) {
        auto collector = TopScoreDocCollector::create(10);
        searcher.search(query, collector.get());
        auto results = collector->topDocs();
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
}

/**
 * Benchmark both modes for comparison
 */
static void BM_Search_Comparison(benchmark::State& state) {
    bool use_batch = state.range(1);  // 0 = one-at-a-time, 1 = batch
    int num_docs = state.range(0);

    std::string index_path = use_batch ? "/tmp/diagon_batch_bench_compare_batch"
                                       : "/tmp/diagon_batch_bench_compare_baseline";

    // Create test index
    auto directory = createTestIndex(index_path, num_docs);

    // Open reader
    auto reader = DirectoryReader::open(*directory);

    // Create searcher with appropriate config
    IndexSearcherConfig config;
    config.enable_batch_scoring = use_batch;
    config.batch_size = 8;
    IndexSearcher searcher(*reader, config);

    // Create query
    TermQuery query(search::Term("body", "search"));

    // Benchmark
    for (auto _ : state) {
        auto collector = TopScoreDocCollector::create(10);
        searcher.search(query, collector.get());
        auto results = collector->topDocs();
        benchmark::DoNotOptimize(results);
    }

    state.SetLabel(use_batch ? "batch" : "baseline");
    state.SetItemsProcessed(state.iterations());
}

// Register benchmarks
BENCHMARK(BM_Search_OneAtATime)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

BENCHMARK(BM_Search_BatchAtATime)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

// Direct comparison (baseline vs batch)
BENCHMARK(BM_Search_Comparison)
    ->Args({10000, 0})  // 10K docs, baseline
    ->Args({10000, 1})  // 10K docs, batch
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

BENCHMARK_MAIN();
