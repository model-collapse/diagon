// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * WAND Performance Benchmark on Reuters-21578 Dataset
 *
 * Uses the standard Reuters-21578 news article dataset for realistic comparison.
 * Dataset: 21,578 news articles from 1987
 * Location: /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/
 */

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Reuters-21578 Dataset Loading ====================

struct ReutersDocument {
    std::string date;
    std::string title;
    std::string body;
};

/**
 * Load a single Reuters document from text file
 */
ReutersDocument loadReutersDocument(const std::string& filepath) {
    ReutersDocument doc;
    std::ifstream file(filepath);
    std::string line;

    // First line is date
    if (std::getline(file, line)) {
        doc.date = line;
    }

    // Skip blank line
    std::getline(file, line);

    // Next line is title
    if (std::getline(file, line)) {
        doc.title = line;
    }

    // Skip blank line
    std::getline(file, line);

    // Rest is body
    std::ostringstream body;
    while (std::getline(file, line)) {
        body << line << " ";
    }
    doc.body = body.str();

    return doc;
}

/**
 * Create index from Reuters-21578 dataset
 */
void createReutersIndex(const fs::path& indexPath) {
    const std::string reutersPath = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";

    if (fs::exists(indexPath)) {
        std::cout << "Using existing Reuters index at " << indexPath << std::endl;
        return;
    }

    std::cout << "Creating Reuters index from " << reutersPath << std::endl;
    fs::create_directories(indexPath);
    auto dir = FSDirectory::open(indexPath.string());

    IndexWriterConfig config;
    config.setRAMBufferSizeMB(128.0);
    IndexWriter writer(*dir, config);

    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS_AND_FREQS;
    ft.stored = false;
    ft.tokenized = true;

    // Load all Reuters documents
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(reutersPath)) {
        if (entry.path().extension() == ".txt") {
            files.push_back(entry.path().string());
        }
    }

    std::cout << "Found " << files.size() << " Reuters documents" << std::endl;

    int indexed = 0;
    for (const auto& filepath : files) {
        try {
            auto reutersDoc = loadReutersDocument(filepath);

            Document doc;
            // Index title and body together as "body" field (like Lucene benchmark)
            std::string text = reutersDoc.title + " " + reutersDoc.body;
            doc.add(std::make_unique<Field>("body", text, ft));

            writer.addDocument(doc);
            indexed++;

            if (indexed % 1000 == 0) {
                std::cout << "  Indexed " << indexed << " documents..." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading " << filepath << ": " << e.what() << std::endl;
        }
    }

    std::cout << "Committing index with " << indexed << " documents..." << std::endl;
    writer.commit();
    std::cout << "Reuters index created (multi-segment)!" << std::endl;
}

// ==================== Global Index Cache ====================

struct IndexCache {
    std::unique_ptr<MMapDirectory> dir;  // MMapDirectory for zero-copy reads
    std::shared_ptr<DirectoryReader> reader;
};

static IndexCache globalCache;

IndexCache& getOrCreateReutersIndex() {
    if (!globalCache.reader) {
        fs::path indexPath = "/tmp/diagon_reuters_index";
        // Create index with FSDirectory (write-optimized)
        createReutersIndex(indexPath);
        // Open reader with MMapDirectory (read-optimized, zero-copy)
        globalCache.dir = MMapDirectory::open(indexPath.string());
        globalCache.reader = DirectoryReader::open(*globalCache.dir);
    }
    return globalCache;
}

// ==================== WAND Benchmarks ====================

/**
 * Benchmark: WAND vs Exhaustive on Reuters (2-term OR query)
 */
static void BM_Reuters_WAND_2Terms(benchmark::State& state) {
    const bool useWAND = state.range(0);
    const int topK = 10;

    auto& cache = getOrCreateReutersIndex();

    IndexSearcherConfig config;
    config.enable_block_max_wand = useWAND;
    IndexSearcher searcher(*cache.reader, config);

    // Common business terms in Reuters
    auto query = BooleanQuery::Builder()
        .add(std::make_shared<TermQuery>(search::Term("body", "market")), Occur::SHOULD)
        .add(std::make_shared<TermQuery>(search::Term("body", "company")), Occur::SHOULD)
        .build();

    // Warmup
    for (int i = 0; i < 10; i++) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    // Benchmark
    for (auto _ : state) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(std::string(useWAND ? "WAND" : "Exhaustive") + " - 2 terms");
}

/**
 * Benchmark: Multi-term OR queries on Reuters
 */
static void BM_Reuters_WAND_MultiTerm(benchmark::State& state) {
    const int numTerms = state.range(0);
    const bool useWAND = state.range(1);
    const int topK = 10;

    auto& cache = getOrCreateReutersIndex();

    IndexSearcherConfig config;
    config.enable_block_max_wand = useWAND;
    IndexSearcher searcher(*cache.reader, config);

    // Common Reuters terms (business/finance vocabulary, 50 terms)
    static const std::vector<std::string> queryTerms = {
        "market", "company", "stock", "trade", "price",
        "bank", "dollar", "oil", "export", "government",
        "share", "billion", "profit", "exchange", "interest",
        "economic", "report", "industry", "investment", "revenue",
        "million", "percent", "year", "said", "would",
        "new", "also", "last", "first", "group",
        "accord", "tax", "rate", "growth", "debt",
        "loss", "quarter", "month", "net", "income",
        "sales", "earnings", "bond", "foreign", "loan",
        "budget", "deficit", "surplus", "inflation", "central"
    };

    BooleanQuery::Builder builder;
    for (int i = 0; i < numTerms && i < static_cast<int>(queryTerms.size()); i++) {
        builder.add(std::make_shared<TermQuery>(search::Term("body", queryTerms[i])), Occur::SHOULD);
    }
    auto query = builder.build();

    // Warmup
    for (int i = 0; i < 10; i++) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    // Benchmark
    for (auto _ : state) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(std::to_string(numTerms) + " terms - " +
                   std::string(useWAND ? "WAND" : "Exhaustive"));
}

/**
 * Benchmark: Different topK values on Reuters
 */
static void BM_Reuters_WAND_TopK(benchmark::State& state) {
    const int topK = state.range(0);
    const bool useWAND = state.range(1);

    auto& cache = getOrCreateReutersIndex();

    IndexSearcherConfig config;
    config.enable_block_max_wand = useWAND;
    IndexSearcher searcher(*cache.reader, config);

    // 3-term OR query
    auto query = BooleanQuery::Builder()
        .add(std::make_shared<TermQuery>(search::Term("body", "market")), Occur::SHOULD)
        .add(std::make_shared<TermQuery>(search::Term("body", "company")), Occur::SHOULD)
        .add(std::make_shared<TermQuery>(search::Term("body", "trade")), Occur::SHOULD)
        .build();

    // Warmup
    for (int i = 0; i < 5; i++) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    // Benchmark
    for (auto _ : state) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("topK=" + std::to_string(topK) + " - " +
                   std::string(useWAND ? "WAND" : "Exhaustive"));
}

/**
 * Benchmark: Rare term query (low selectivity)
 */
static void BM_Reuters_WAND_RareTerm(benchmark::State& state) {
    const bool useWAND = state.range(0);
    const int topK = 10;

    auto& cache = getOrCreateReutersIndex();

    IndexSearcherConfig config;
    config.enable_block_max_wand = useWAND;
    IndexSearcher searcher(*cache.reader, config);

    // Rare terms with low document frequency
    auto query = BooleanQuery::Builder()
        .add(std::make_shared<TermQuery>(search::Term("body", "cocoa")), Occur::SHOULD)
        .add(std::make_shared<TermQuery>(search::Term("body", "coffee")), Occur::SHOULD)
        .build();

    // Warmup
    for (int i = 0; i < 10; i++) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    // Benchmark
    for (auto _ : state) {
        auto results = searcher.search(*query, topK);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(std::string(useWAND ? "WAND" : "Exhaustive") + " - rare terms");
}

// ==================== Benchmark Registration ====================

// 2-term OR queries
BENCHMARK(BM_Reuters_WAND_2Terms)
    ->Arg(0)    // Exhaustive
    ->Arg(1)    // WAND
    ->Unit(benchmark::kMicrosecond);

// Multi-term OR queries
BENCHMARK(BM_Reuters_WAND_MultiTerm)
    ->Args({2, 0})    // 2 terms, exhaustive
    ->Args({2, 1})    // 2 terms, WAND
    ->Args({5, 0})    // 5 terms, exhaustive
    ->Args({5, 1})    // 5 terms, WAND
    ->Args({10, 0})   // 10 terms, exhaustive
    ->Args({10, 1})   // 10 terms, WAND
    ->Args({20, 0})   // 20 terms, exhaustive
    ->Args({20, 1})   // 20 terms, WAND
    ->Args({50, 0})   // 50 terms, exhaustive
    ->Args({50, 1})   // 50 terms, WAND
    ->Unit(benchmark::kMicrosecond);

// Different topK values
BENCHMARK(BM_Reuters_WAND_TopK)
    ->Args({10, 0})     // top-10, exhaustive
    ->Args({10, 1})     // top-10, WAND
    ->Args({100, 0})    // top-100, exhaustive
    ->Args({100, 1})    // top-100, WAND
    ->Args({1000, 0})   // top-1000, exhaustive
    ->Args({1000, 1})   // top-1000, WAND
    ->Unit(benchmark::kMicrosecond);

// Rare term queries
BENCHMARK(BM_Reuters_WAND_RareTerm)
    ->Arg(0)    // Exhaustive
    ->Arg(1)    // WAND
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
