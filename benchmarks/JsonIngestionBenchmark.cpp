// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

//
// JsonIngestionBenchmark - Compare JSON parsing vs manual field construction
//
// Measures document creation throughput via two paths:
//   1. JsonDocumentParser::parse() — single JSON string → Document
//   2. Manual Field construction — TextField, Field(long), Field(double), StringField
//
// Both paths produce equivalent documents with the same field types.
// This isolates the JSON parsing overhead from field construction cost.
//

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/document/JsonDocumentParser.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <benchmark/benchmark.h>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Shared Constants ====================

static constexpr int kNumDocs = 10000;

// Body text matching the task spec — realistic paragraph length
static const char* kBodyText =
    "Lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua Ut enim ad minim veniam "
    "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
    "consequat Duis aute irure dolor in reprehenderit in voluptate velit esse";

// FieldTypes matching JsonDocumentParser's internal types exactly
static FieldType makeIndexedLongType() {
    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS;
    ft.stored = true;
    ft.tokenized = false;
    ft.docValuesType = DocValuesType::NUMERIC;
    ft.numericType = NumericType::LONG;
    return ft;
}

static FieldType makeIndexedDoubleType() {
    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS;
    ft.stored = true;
    ft.tokenized = false;
    ft.docValuesType = DocValuesType::NUMERIC;
    ft.numericType = NumericType::DOUBLE;
    return ft;
}

static const FieldType INDEXED_LONG_TYPE = makeIndexedLongType();
static const FieldType INDEXED_DOUBLE_TYPE = makeIndexedDoubleType();

// ==================== Pre-generate JSON strings ====================

static std::vector<std::string> generateJsonStrings(int n) {
    std::vector<std::string> jsons;
    jsons.reserve(n);
    for (int i = 0; i < n; i++) {
        // Matches task spec: {"title":"Doc N","body":"...","count":N,"price":9.99,"active":true}
        std::string json = R"({"title":"Doc )" + std::to_string(i) +
                           R"(","body":")" + kBodyText +
                           R"(","count":)" + std::to_string(i) +
                           R"(,"price":9.99,"active":true})";
        jsons.push_back(std::move(json));
    }
    return jsons;
}

// ==================== Document Creation Only (no indexing) ====================

/**
 * BM_JsonParse — Create documents via JsonDocumentParser::parse()
 *
 * Measures: JSON parsing + nlohmann::json + field allocation
 * Excludes: indexing, tokenization, disk I/O
 */
static void BM_JsonParse(benchmark::State& state) {
    auto jsons = generateJsonStrings(kNumDocs);

    for (auto _ : state) {
        for (const auto& json : jsons) {
            auto doc = JsonDocumentParser::parse(json.c_str(), json.size());
            benchmark::DoNotOptimize(doc.get());
        }
    }

    state.SetItemsProcessed(state.iterations() * kNumDocs);
    state.SetLabel(std::to_string(kNumDocs) + " docs (parse only)");
}

/**
 * BM_ManualConstruct — Create documents via manual Field construction
 *
 * Produces identical field types to JsonDocumentParser for fair comparison:
 *   title  → TextField (stored, tokenized)
 *   body   → TextField (stored, tokenized)
 *   count  → Field with INDEXED_LONG_TYPE
 *   price  → Field with INDEXED_DOUBLE_TYPE (bit_cast<int64_t>)
 *   active → StringField (stored, "true")
 */
static void BM_ManualConstruct(benchmark::State& state) {
    // Pre-compute price bits once (same as JsonDocumentParser does)
    const int64_t priceBits = std::bit_cast<int64_t>(9.99);

    for (auto _ : state) {
        for (int i = 0; i < kNumDocs; i++) {
            auto doc = std::make_unique<Document>();
            doc->add(std::make_unique<TextField>("title", "Doc " + std::to_string(i), true));
            doc->add(std::make_unique<TextField>("body", std::string(kBodyText), true));
            doc->add(std::make_unique<Field>("count", static_cast<int64_t>(i), INDEXED_LONG_TYPE));
            doc->add(std::make_unique<Field>("price", priceBits, INDEXED_DOUBLE_TYPE));
            doc->add(std::make_unique<StringField>("active", "true", true));
            benchmark::DoNotOptimize(doc.get());
        }
    }

    state.SetItemsProcessed(state.iterations() * kNumDocs);
    state.SetLabel(std::to_string(kNumDocs) + " docs (construct only)");
}

/**
 * BM_JsonParseBatch — Create documents via JsonDocumentParser::parseBatch()
 *
 * Parses a single JSON array containing all documents at once.
 * Tests whether batch parsing amortizes nlohmann::json overhead.
 */
static void BM_JsonParseBatch(benchmark::State& state) {
    // Build one big JSON array: [{"title":"Doc 0",...}, {"title":"Doc 1",...}, ...]
    std::string batchJson = "[";
    for (int i = 0; i < kNumDocs; i++) {
        if (i > 0) batchJson += ",";
        batchJson += R"({"title":"Doc )" + std::to_string(i) +
                     R"(","body":")" + kBodyText +
                     R"(","count":)" + std::to_string(i) +
                     R"(,"price":9.99,"active":true})";
    }
    batchJson += "]";

    for (auto _ : state) {
        auto docs = JsonDocumentParser::parseBatch(batchJson.c_str(), batchJson.size());
        benchmark::DoNotOptimize(docs.data());
    }

    state.SetItemsProcessed(state.iterations() * kNumDocs);
    state.SetLabel(std::to_string(kNumDocs) + " docs (batch parse)");
}

// ==================== End-to-End: Creation + Indexing ====================

/**
 * BM_JsonParseAndIndex — JSON parse + full indexing pipeline
 *
 * Measures: JSON parsing → Document → IndexWriter.addDocument() → commit
 * This is the realistic path for CGO/FFI callers sending JSON.
 */
static void BM_JsonParseAndIndex(benchmark::State& state) {
    auto jsons = generateJsonStrings(kNumDocs);

    for (auto _ : state) {
        state.PauseTiming();
        auto tempDir = fs::temp_directory_path() / "diagon_json_bench_json";
        fs::remove_all(tempDir);
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());
        IndexWriterConfig config;
        config.setRAMBufferSizeMB(64.0);
        config.setMaxBufferedDocs(50000);
        IndexWriter writer(*dir, config);
        state.ResumeTiming();

        for (const auto& json : jsons) {
            auto doc = JsonDocumentParser::parse(json.c_str(), json.size());
            writer.addDocument(*doc);
        }
        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * kNumDocs);
    state.SetLabel(std::to_string(kNumDocs) + " docs (JSON → index)");
}

/**
 * BM_ManualConstructAndIndex — Manual field construction + full indexing
 *
 * Measures: Field allocation → Document → IndexWriter.addDocument() → commit
 * This is the baseline for native C++ callers using the Field API directly.
 */
static void BM_ManualConstructAndIndex(benchmark::State& state) {
    const int64_t priceBits = std::bit_cast<int64_t>(9.99);

    for (auto _ : state) {
        state.PauseTiming();
        auto tempDir = fs::temp_directory_path() / "diagon_json_bench_manual";
        fs::remove_all(tempDir);
        fs::create_directories(tempDir);
        auto dir = FSDirectory::open(tempDir.string());
        IndexWriterConfig config;
        config.setRAMBufferSizeMB(64.0);
        config.setMaxBufferedDocs(50000);
        IndexWriter writer(*dir, config);
        state.ResumeTiming();

        for (int i = 0; i < kNumDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("title", "Doc " + std::to_string(i), true));
            doc.add(std::make_unique<TextField>("body", std::string(kBodyText), true));
            doc.add(std::make_unique<Field>("count", static_cast<int64_t>(i), INDEXED_LONG_TYPE));
            doc.add(std::make_unique<Field>("price", priceBits, INDEXED_DOUBLE_TYPE));
            doc.add(std::make_unique<StringField>("active", "true", true));
            writer.addDocument(doc);
        }
        writer.commit();

        state.PauseTiming();
        dir->close();
        fs::remove_all(tempDir);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * kNumDocs);
    state.SetLabel(std::to_string(kNumDocs) + " docs (manual → index)");
}

// ==================== Benchmark Registrations ====================

// Document creation only (isolate construction cost)
BENCHMARK(BM_JsonParse)->Unit(benchmark::kMillisecond)->Iterations(5);
BENCHMARK(BM_ManualConstruct)->Unit(benchmark::kMillisecond)->Iterations(5);
BENCHMARK(BM_JsonParseBatch)->Unit(benchmark::kMillisecond)->Iterations(5);

// End-to-end: creation + indexing
BENCHMARK(BM_JsonParseAndIndex)->Unit(benchmark::kMillisecond)->Iterations(3);
BENCHMARK(BM_ManualConstructAndIndex)->Unit(benchmark::kMillisecond)->Iterations(3);

BENCHMARK_MAIN();
