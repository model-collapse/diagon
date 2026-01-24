// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>
#include <sstream>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Helper Functions ====================

/**
 * Generate random text for document
 */
std::string generateRandomText(int numWords, std::mt19937& rng) {
    static const std::vector<std::string> words = {
        "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
        "search", "engine", "index", "document", "query", "result", "score",
        "lucene", "elasticsearch", "solr", "database", "algorithm", "data",
        "performance", "benchmark", "optimization", "memory", "disk", "cache",
        "distributed", "scalable", "fast", "efficient", "robust", "reliable"
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
 * Create field type for benchmarking
 */
FieldType createIndexedFieldType() {
    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    ft.stored = true;
    ft.tokenized = true;
    return ft;
}

// ==================== Indexing Benchmarks ====================

/**
 * Benchmark: Basic document indexing
 * Measures throughput of adding documents to index
 */
static void BM_IndexDocuments(benchmark::State& state) {
    const int numDocs = state.range(0);
    const int wordsPerDoc = 50;

    std::mt19937 rng(42);  // Fixed seed for reproducibility

    for (auto _ : state) {
        state.PauseTiming();

        // Create temp directory
        auto tempDir = fs::temp_directory_path() / ("diagon_bench_" + std::to_string(rng()));
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());

        // Configure writer
        IndexWriterConfig config;
        config.setRAMBufferSizeMB(16.0);  // 16MB buffer

        IndexWriter writer(*dir, config);
        FieldType ft = createIndexedFieldType();

        state.ResumeTiming();

        // Index documents
        for (int i = 0; i < numDocs; i++) {
            Document doc;
            std::string text = generateRandomText(wordsPerDoc, rng);
            doc.add(std::make_unique<Field>("body", text, ft));
            writer.addDocument(doc);
        }

        writer.commit();

        state.PauseTiming();

        // Cleanup
        dir->close();
        fs::remove_all(tempDir);

        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetLabel(std::to_string(numDocs) + " docs");
}

/**
 * Benchmark: Indexing with different RAM buffer sizes
 * Measures impact of RAM buffer size on throughput
 */
static void BM_IndexWithDifferentRAMBuffers(benchmark::State& state) {
    const int numDocs = 1000;
    const int wordsPerDoc = 50;
    const double ramBufferMB = state.range(0);

    std::mt19937 rng(42);

    for (auto _ : state) {
        state.PauseTiming();

        auto tempDir = fs::temp_directory_path() / ("diagon_bench_" + std::to_string(rng()));
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());

        IndexWriterConfig config;
        config.setRAMBufferSizeMB(ramBufferMB);

        IndexWriter writer(*dir, config);
        FieldType ft = createIndexedFieldType();

        state.ResumeTiming();

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            std::string text = generateRandomText(wordsPerDoc, rng);
            doc.add(std::make_unique<Field>("body", text, ft));
            writer.addDocument(doc);
        }

        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetLabel(std::to_string((int)ramBufferMB) + "MB RAM");
}

/**
 * Benchmark: Commit overhead
 * Measures time spent in commit operation
 */
static void BM_CommitOverhead(benchmark::State& state) {
    const int numDocs = state.range(0);
    const int wordsPerDoc = 50;

    std::mt19937 rng(42);

    for (auto _ : state) {
        state.PauseTiming();

        auto tempDir = fs::temp_directory_path() / ("diagon_bench_" + std::to_string(rng()));
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());

        IndexWriterConfig config;
        IndexWriter writer(*dir, config);
        FieldType ft = createIndexedFieldType();

        // Index documents (not timed)
        for (int i = 0; i < numDocs; i++) {
            Document doc;
            std::string text = generateRandomText(wordsPerDoc, rng);
            doc.add(std::make_unique<Field>("body", text, ft));
            writer.addDocument(doc);
        }

        state.ResumeTiming();

        // Time only commit
        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetLabel(std::to_string(numDocs) + " docs commit");
}

/**
 * Benchmark: Document size impact
 * Measures how document size affects indexing throughput
 */
static void BM_IndexDifferentDocSizes(benchmark::State& state) {
    const int numDocs = 500;
    const int wordsPerDoc = state.range(0);

    std::mt19937 rng(42);

    for (auto _ : state) {
        state.PauseTiming();

        auto tempDir = fs::temp_directory_path() / ("diagon_bench_" + std::to_string(rng()));
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());

        IndexWriterConfig config;
        IndexWriter writer(*dir, config);
        FieldType ft = createIndexedFieldType();

        state.ResumeTiming();

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            std::string text = generateRandomText(wordsPerDoc, rng);
            doc.add(std::make_unique<Field>("body", text, ft));
            writer.addDocument(doc);
        }

        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetLabel(std::to_string(wordsPerDoc) + " words/doc");
}

// ==================== Benchmark Registrations ====================

// Basic indexing with different document counts
BENCHMARK(BM_IndexDocuments)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000)
    ->Arg(5000)
    ->Unit(benchmark::kMillisecond);

// RAM buffer size impact
BENCHMARK(BM_IndexWithDifferentRAMBuffers)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Unit(benchmark::kMillisecond);

// Commit overhead
BENCHMARK(BM_CommitOverhead)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond);

// Document size impact
BENCHMARK(BM_IndexDifferentDocSizes)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(200)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
