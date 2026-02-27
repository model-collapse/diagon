// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/IndexWriter.h"
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
        "the",       "quick",     "brown",   "fox",         "jumps",         "over",
        "lazy",      "dog",       "search",  "engine",      "index",         "document",
        "query",     "result",    "score",   "lucene",      "elasticsearch", "solr",
        "database",  "algorithm", "data",    "performance", "benchmark",     "optimization",
        "memory",    "disk",      "cache",   "distributed", "scalable",      "fast",
        "efficient", "robust",    "reliable"};

    std::uniform_int_distribution<> dist(0, words.size() - 1);
    std::ostringstream oss;

    for (int i = 0; i < numWords; i++) {
        if (i > 0)
            oss << " ";
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

// ==================== Multi-Field Benchmarks (Issue #6 workload) ====================

/**
 * Benchmark: Multi-field document indexing (matches Issue #6 CGO workload)
 *
 * Issue #6 reports 25-field documents via C API at ~8,900 docs/sec.
 * This benchmark measures the C++ side with 25 text fields per document,
 * isolating the C++ indexing path from CGO overhead.
 */
static void BM_IndexMultiFieldDocuments(benchmark::State& state) {
    const int numDocs = state.range(0);
    const int numFields = 25;  // Match Issue #6 workload
    const int wordsPerField = 20;

    std::mt19937 rng(42);

    for (auto _ : state) {
        state.PauseTiming();

        auto tempDir = fs::temp_directory_path() / ("diagon_bench_mf_" + std::to_string(rng()));
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());

        IndexWriterConfig config;
        config.setRAMBufferSizeMB(64.0);
        config.setMaxBufferedDocs(50000);

        IndexWriter writer(*dir, config);
        FieldType ft = createIndexedFieldType();

        // Pre-generate field names
        std::vector<std::string> fieldNames;
        fieldNames.reserve(numFields);
        for (int f = 0; f < numFields; f++) {
            fieldNames.push_back("field_" + std::to_string(f));
        }

        state.ResumeTiming();

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            for (int f = 0; f < numFields; f++) {
                std::string text = generateRandomText(wordsPerField, rng);
                doc.add(std::make_unique<Field>(fieldNames[f], text, ft));
            }
            writer.addDocument(doc);
        }

        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetLabel(std::to_string(numDocs) + " docs x " + std::to_string(numFields) + " fields");
}

/**
 * Benchmark: Batch addDocuments() vs single addDocument()
 *
 * Measures the throughput improvement from the batch API that acquires
 * the DocumentsWriter mutex once per batch instead of once per document.
 */
static void BM_IndexBatchDocuments(benchmark::State& state) {
    const int batchSize = state.range(0);
    const int totalDocs = 5000;
    const int numFields = 10;
    const int wordsPerField = 20;

    std::mt19937 rng(42);

    for (auto _ : state) {
        state.PauseTiming();

        auto tempDir = fs::temp_directory_path() / ("diagon_bench_batch_" + std::to_string(rng()));
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());

        IndexWriterConfig config;
        config.setRAMBufferSizeMB(64.0);
        config.setMaxBufferedDocs(50000);

        IndexWriter writer(*dir, config);
        FieldType ft = createIndexedFieldType();

        std::vector<std::string> fieldNames;
        for (int f = 0; f < numFields; f++) {
            fieldNames.push_back("field_" + std::to_string(f));
        }

        state.ResumeTiming();

        if (batchSize == 1) {
            // Single-document path (baseline)
            for (int i = 0; i < totalDocs; i++) {
                Document doc;
                for (int f = 0; f < numFields; f++) {
                    std::string text = generateRandomText(wordsPerField, rng);
                    doc.add(std::make_unique<Field>(fieldNames[f], text, ft));
                }
                writer.addDocument(doc);
            }
        } else {
            // Batch path
            for (int i = 0; i < totalDocs; i += batchSize) {
                int thisBatch = std::min(batchSize, totalDocs - i);

                // Build batch
                std::vector<Document> docs(thisBatch);
                std::vector<const Document*> docPtrs;
                docPtrs.reserve(thisBatch);

                for (int j = 0; j < thisBatch; j++) {
                    for (int f = 0; f < numFields; f++) {
                        std::string text = generateRandomText(wordsPerField, rng);
                        docs[j].add(std::make_unique<Field>(fieldNames[f], text, ft));
                    }
                    docPtrs.push_back(&docs[j]);
                }

                writer.addDocuments(docPtrs);
            }
        }

        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * totalDocs);
    state.SetLabel("batch=" + std::to_string(batchSize));
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
BENCHMARK(BM_CommitOverhead)->Arg(100)->Arg(500)->Arg(1000)->Unit(benchmark::kMillisecond);

// Document size impact
BENCHMARK(BM_IndexDifferentDocSizes)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(200)
    ->Unit(benchmark::kMillisecond);

// Multi-field indexing (Issue #6 workload: 25 fields per doc)
BENCHMARK(BM_IndexMultiFieldDocuments)
    ->Arg(1000)
    ->Arg(5000)
    ->Arg(10000)
    ->Unit(benchmark::kMillisecond);

// Batch vs single document (mutex amortization)
BENCHMARK(BM_IndexBatchDocuments)
    ->Arg(1)     // Single-document baseline
    ->Arg(50)    // Small batch
    ->Arg(200)   // Medium batch
    ->Arg(500)   // Large batch (Issue #6 target)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
