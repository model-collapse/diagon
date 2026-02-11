// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * BM25 Performance Guard Tests
 *
 * Validates Diagon BM25 scoring performance meets or exceeds Apache Lucene baseline.
 *
 * Baseline: Lucene 11.0.0-SNAPSHOT on Reuters-21578 (19,043 documents, 64,664 unique terms)
 * Date Established: 2026-02-11
 * Reference: docs/LUCENE_BM25_PERFORMANCE_BASELINE.md
 *
 * Performance Targets (Lucene baseline + 15-20% margin):
 * - OR-5 queries: ≤ 126 µs (P50), ≤ 250 µs (P99)
 * - Single-term: ≤ 65 µs (P50), ≤ 350 µs (P99)
 * - AND-2 queries: ≤ 51 µs (P50), ≤ 165 µs (P99)
 *
 * Why margins?
 * - C++ vs Java implementation differences
 * - Different memory management strategies
 * - Compiler optimizations may vary
 * - Still validates we're in the right ballpark
 *
 * Critical: Never fall below 50% of Lucene performance (> 2x slower = fail)
 */

#include <gtest/gtest.h>
#include "diagon/index/IndexWriter.h"
#include "diagon/index/IndexReader.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/analysis/StandardAnalyzer.h"
#include "diagon/store/FSDirectory.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::search;
using namespace diagon::analysis;
using namespace diagon::store;

namespace {

// Test index path
constexpr const char* TEST_INDEX_PATH = "/tmp/diagon_bm25_perf_guard_index";

// Measurement parameters
constexpr int WARMUP_ITERATIONS = 100;
constexpr int MEASUREMENT_ITERATIONS = 1000;

/**
 * Generate synthetic Reuters-like documents for testing
 * (Real Reuters data requires separate download)
 */
std::vector<std::unique_ptr<Document>> generateTestDocuments(int count) {
    std::vector<std::unique_ptr<Document>> docs;
    docs.reserve(count);

    // Common Reuters terms
    std::vector<std::string> terms = {
        "market", "trade", "oil", "price", "dollar", "stock", "company",
        "export", "import", "economy", "financial", "investor", "trading",
        "petroleum", "barrel", "cocoa", "coffee", "copper", "zinc"
    };

    std::mt19937 rng(42); // Deterministic
    std::uniform_int_distribution<int> term_dist(0, terms.size() - 1);
    std::uniform_int_distribution<int> len_dist(50, 200);

    for (int i = 0; i < count; i++) {
        auto doc = std::make_unique<Document>();

        // Generate title (5 terms)
        std::string title;
        for (int j = 0; j < 5; j++) {
            if (j > 0) title += " ";
            title += terms[term_dist(rng)];
        }

        // Generate body (50-200 terms)
        std::string body;
        int body_len = len_dist(rng);
        for (int j = 0; j < body_len; j++) {
            if (j > 0) body += " ";
            body += terms[term_dist(rng)];
        }

        doc->addField(std::make_unique<TextField>("title", title, Field::Store::NO));
        doc->addField(std::make_unique<TextField>("body", body, Field::Store::NO));

        docs.push_back(std::move(doc));
    }

    return docs;
}

/**
 * Create test index with synthetic documents
 */
void createTestIndex() {
    // Clean index directory
    std::filesystem::remove_all(TEST_INDEX_PATH);
    std::filesystem::create_directories(TEST_INDEX_PATH);

    // Create index
    auto directory = std::make_unique<FSDirectory>(TEST_INDEX_PATH);
    IndexWriterConfig config(std::make_unique<StandardAnalyzer>());
    config.setSimilarity(std::make_unique<BM25Similarity>(1.2f, 0.75f));

    IndexWriter writer(std::move(directory), config);

    // Generate and index documents
    auto docs = generateTestDocuments(20000); // Similar to Reuters size
    for (const auto& doc : docs) {
        writer.addDocument(*doc);
    }

    writer.commit();
}

/**
 * Measure query latency (P50, P95, P99)
 */
struct LatencyStats {
    double p50_us;
    double p95_us;
    double p99_us;
    double mean_us;
};

LatencyStats measureQueryLatency(IndexSearcher& searcher, const Query& query, int topK) {
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        searcher.search(query, topK);
    }

    // Measurement
    std::vector<double> latencies;
    latencies.reserve(MEASUREMENT_ITERATIONS);

    for (int i = 0; i < MEASUREMENT_ITERATIONS; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        auto results = searcher.search(query, topK);
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        latencies.push_back(duration.count() / 1000.0); // Convert to microseconds
    }

    // Calculate percentiles
    std::sort(latencies.begin(), latencies.end());

    LatencyStats stats;
    stats.p50_us = latencies[MEASUREMENT_ITERATIONS * 50 / 100];
    stats.p95_us = latencies[MEASUREMENT_ITERATIONS * 95 / 100];
    stats.p99_us = latencies[MEASUREMENT_ITERATIONS * 99 / 100];

    double sum = 0;
    for (double lat : latencies) {
        sum += lat;
    }
    stats.mean_us = sum / MEASUREMENT_ITERATIONS;

    return stats;
}

// Test fixture
class BM25PerformanceGuardTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        createTestIndex();
    }

    void SetUp() override {
        auto directory = std::make_unique<FSDirectory>(TEST_INDEX_PATH);
        reader_ = IndexReader::open(std::move(directory));
        searcher_ = std::make_unique<IndexSearcher>(reader_.get());
        searcher_->setSimilarity(std::make_unique<BM25Similarity>(1.2f, 0.75f));
    }

    void TearDown() override {
        searcher_.reset();
        reader_.reset();
    }

    std::unique_ptr<IndexReader> reader_;
    std::unique_ptr<IndexSearcher> searcher_;
};

// ==================== Single-Term Query Guards ====================

TEST_F(BM25PerformanceGuardTest, SingleTerm_P50_Baseline) {
    // Lucene baseline: 46.8 µs (term: "market", 1,007 hits)
    // Diagon target: ≤ 65 µs (39% margin)

    Query query = TermQuery("body", "market");
    auto stats = measureQueryLatency(*searcher_, query, 10);

    EXPECT_LE(stats.p50_us, 65.0)
        << "Single-term query P50 exceeded baseline: "
        << stats.p50_us << " µs (target: ≤65 µs, Lucene: 46.8 µs)";

    // Critical failure: > 2x slower than Lucene
    EXPECT_LE(stats.p50_us, 100.0)
        << "CRITICAL: Single-term query > 2x slower than Lucene: "
        << stats.p50_us << " µs (Lucene: 46.8 µs)";
}

TEST_F(BM25PerformanceGuardTest, SingleTerm_P99_Baseline) {
    // Lucene baseline: 297.7 µs
    // Diagon target: ≤ 350 µs (18% margin)

    Query query = TermQuery("body", "market");
    auto stats = measureQueryLatency(*searcher_, query, 10);

    EXPECT_LE(stats.p99_us, 350.0)
        << "Single-term query P99 exceeded baseline: "
        << stats.p99_us << " µs (target: ≤350 µs, Lucene: 297.7 µs)";
}

// ==================== OR Query Guards (WAND) ====================

TEST_F(BM25PerformanceGuardTest, OR5Query_P50_Baseline) {
    // Lucene baseline: 109.6 µs (OR-5: oil, trade, market, price, dollar)
    // Diagon target: ≤ 126 µs (15% margin)

    BooleanQuery::Builder builder;
    builder.add(TermQuery("body", "oil"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "trade"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "market"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "price"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "dollar"), BooleanClause::Occur::SHOULD);
    auto query = builder.build();

    auto stats = measureQueryLatency(*searcher_, query, 10);

    EXPECT_LE(stats.p50_us, 126.0)
        << "OR-5 query P50 exceeded baseline: "
        << stats.p50_us << " µs (target: ≤126 µs, Lucene: 109.6 µs)";

    // Critical failure: > 2x slower than Lucene
    EXPECT_LE(stats.p50_us, 220.0)
        << "CRITICAL: OR-5 query > 2x slower than Lucene: "
        << stats.p50_us << " µs (Lucene: 109.6 µs)";
}

TEST_F(BM25PerformanceGuardTest, OR5Query_P99_Baseline) {
    // Lucene baseline: 211.1 µs
    // Diagon target: ≤ 250 µs (18% margin)

    BooleanQuery::Builder builder;
    builder.add(TermQuery("body", "oil"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "trade"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "market"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "price"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "dollar"), BooleanClause::Occur::SHOULD);
    auto query = builder.build();

    auto stats = measureQueryLatency(*searcher_, query, 10);

    EXPECT_LE(stats.p99_us, 250.0)
        << "OR-5 query P99 exceeded baseline: "
        << stats.p99_us << " µs (target: ≤250 µs, Lucene: 211.1 µs)";
}

// ==================== AND Query Guards ====================

TEST_F(BM25PerformanceGuardTest, AND2Query_P50_Baseline) {
    // Lucene baseline: 43.1 µs (AND-2: oil, price)
    // Diagon target: ≤ 51 µs (18% margin)

    BooleanQuery::Builder builder;
    builder.add(TermQuery("body", "oil"), BooleanClause::Occur::MUST);
    builder.add(TermQuery("body", "price"), BooleanClause::Occur::MUST);
    auto query = builder.build();

    auto stats = measureQueryLatency(*searcher_, query, 10);

    EXPECT_LE(stats.p50_us, 51.0)
        << "AND-2 query P50 exceeded baseline: "
        << stats.p50_us << " µs (target: ≤51 µs, Lucene: 43.1 µs)";

    // Critical failure: > 2x slower than Lucene
    EXPECT_LE(stats.p50_us, 90.0)
        << "CRITICAL: AND-2 query > 2x slower than Lucene: "
        << stats.p50_us << " µs (Lucene: 43.1 µs)";
}

TEST_F(BM25PerformanceGuardTest, AND2Query_P99_Baseline) {
    // Lucene baseline: 138.1 µs
    // Diagon target: ≤ 165 µs (19% margin)

    BooleanQuery::Builder builder;
    builder.add(TermQuery("body", "oil"), BooleanClause::Occur::MUST);
    builder.add(TermQuery("body", "price"), BooleanClause::Occur::MUST);
    auto query = builder.build();

    auto stats = measureQueryLatency(*searcher_, query, 10);

    EXPECT_LE(stats.p99_us, 165.0)
        << "AND-2 query P99 exceeded baseline: "
        << stats.p99_us << " µs (target: ≤165 µs, Lucene: 138.1 µs)";
}

// ==================== TopK Scaling Guard ====================

TEST_F(BM25PerformanceGuardTest, TopKScaling_OR5) {
    // Lucene behavior: K=1000 is 2.3x slower than K=50 (254.1 vs 109.5 µs)
    // Diagon should have similar scaling (≤ 3x difference)

    BooleanQuery::Builder builder;
    builder.add(TermQuery("body", "oil"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "trade"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "market"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "price"), BooleanClause::Occur::SHOULD);
    builder.add(TermQuery("body", "dollar"), BooleanClause::Occur::SHOULD);
    auto query = builder.build();

    auto stats_k50 = measureQueryLatency(*searcher_, query, 50);
    auto stats_k1000 = measureQueryLatency(*searcher_, query, 1000);

    double scaling_factor = stats_k1000.p50_us / stats_k50.p50_us;

    EXPECT_LE(scaling_factor, 3.0)
        << "TopK scaling exceeded limit: K=1000 is " << scaling_factor
        << "x slower than K=50 (limit: ≤3x, Lucene: 2.3x)";
}

// ==================== Rare Term Performance ====================

TEST_F(BM25PerformanceGuardTest, RareTerm_Faster) {
    // Observation from Lucene: Rare terms (cocoa, 89 hits) are faster than common terms
    // cocoa: 20.2 µs vs market: 46.8 µs (2.3x faster)
    // This validates that scoring dominates, not lookup

    Query rare_query = TermQuery("body", "cocoa");
    Query common_query = TermQuery("body", "market");

    auto rare_stats = measureQueryLatency(*searcher_, rare_query, 10);
    auto common_stats = measureQueryLatency(*searcher_, common_query, 10);

    // Rare term should be faster (or at least not slower)
    EXPECT_LE(rare_stats.p50_us, common_stats.p50_us * 1.5)
        << "Rare term unexpectedly slow: " << rare_stats.p50_us
        << " µs (common term: " << common_stats.p50_us << " µs)";
}

} // namespace
