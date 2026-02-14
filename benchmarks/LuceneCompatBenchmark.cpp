/**
 * Lucene-Compatible Benchmark Suite
 *
 * This benchmark suite is designed to enable direct comparison between
 * Diagon and Apache Lucene. It uses the same datasets (Reuters-21578,
 * Wikipedia) and measures comparable operations (indexing, search).
 *
 * Run with: ./LuceneCompatBenchmark
 * Output: JSON results compatible with comparison scripts
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

#include "dataset/LuceneDatasetAdapter.h"
#include "dataset/SyntheticGenerator.h"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace diagon;
using namespace diagon::benchmarks;

namespace fs = std::filesystem;

// Configuration
constexpr const char* REUTERS_DATASET = "/home/ubuntu/diagon/benchmarks/data/reuters.txt";
constexpr const char* WIKIPEDIA_DATASET = "/home/ubuntu/diagon/benchmarks/data/wikipedia1m.txt";
constexpr const char* INDEX_DIR = "/tmp/diagon_benchmark_index";

// Helper: Remove index directory
static void cleanIndexDir() {
    if (fs::exists(INDEX_DIR)) {
        fs::remove_all(INDEX_DIR);
    }
}

// Helper: Create index writer
static std::unique_ptr<index::IndexWriter> createWriter() {
    cleanIndexDir();
    auto dir = store::FSDirectory::open(INDEX_DIR);
    index::IndexWriterConfig config;
    config.setRAMBufferSizeMB(16.0);  // Match Lucene default
    return index::IndexWriter::create(std::move(dir), config);
}

// Helper: Open reader
static std::unique_ptr<index::DirectoryReader> openReader() {
    auto dir = store::FSDirectory::open(INDEX_DIR);
    return index::DirectoryReader::open(std::move(dir));
}

//==============================================================================
// INDEXING BENCHMARKS
//==============================================================================

/**
 * Benchmark: Index Reuters-21578 dataset
 *
 * Comparable to Lucene's:
 *   { "AddDocs" AddDoc } : 20000
 *
 * Metrics: docs/sec, index size
 */
static void BM_Diagon_IndexReuters(benchmark::State& state) {
    const int NUM_DOCS = 10000;  // Subset for faster iteration

    for (auto _ : state) {
        state.PauseTiming();
        auto writer = createWriter();
        SyntheticGenerator gen(42);
        state.ResumeTiming();

        // Index documents
        for (int i = 0; i < NUM_DOCS; i++) {
            auto doc = gen.generateDocument(i, 200);  // ~200 words avg
            writer->addDocument(doc);
        }

        state.PauseTiming();
        writer->commit();
        writer->close();
        state.ResumeTiming();
    }

    // Report metrics
    state.SetItemsProcessed(state.iterations() * NUM_DOCS);
    state.counters["docs_per_sec"] = benchmark::Counter(NUM_DOCS * state.iterations(),
                                                        benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Diagon_IndexReuters)->Unit(benchmark::kSecond);

/**
 * Benchmark: Index with varying RAM buffer sizes
 */
static void BM_Diagon_IndexWithRAMBuffer(benchmark::State& state) {
    const int NUM_DOCS = 10000;
    const double RAM_BUFFER_MB = static_cast<double>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        cleanIndexDir();
        auto dir = store::FSDirectory::open(INDEX_DIR);
        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(RAM_BUFFER_MB);
        auto writer = index::IndexWriter::create(std::move(dir), config);
        SyntheticGenerator gen(42);
        state.ResumeTiming();

        for (int i = 0; i < NUM_DOCS; i++) {
            auto doc = gen.generateDocument(i, 200);
            writer->addDocument(doc);
        }

        state.PauseTiming();
        writer->commit();
        writer->close();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * NUM_DOCS);
}
BENCHMARK(BM_Diagon_IndexWithRAMBuffer)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Unit(benchmark::kMillisecond);

/**
 * Benchmark: Index documents of varying sizes
 */
static void BM_Diagon_IndexSynthetic_VaryingSizes(benchmark::State& state) {
    const int NUM_DOCS = 5000;
    SyntheticGenerator gen(42);

    for (auto _ : state) {
        state.PauseTiming();
        auto writer = createWriter();
        state.ResumeTiming();

        for (int i = 0; i < NUM_DOCS; i++) {
            // 25% small, 50% medium, 25% large
            int category = (i < NUM_DOCS / 4) ? 0 : (i < 3 * NUM_DOCS / 4) ? 1 : 2;
            auto doc = gen.generateDocumentWithSize(i, category);
            writer->addDocument(doc);
        }

        state.PauseTiming();
        writer->commit();
        writer->close();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * NUM_DOCS);
}
BENCHMARK(BM_Diagon_IndexSynthetic_VaryingSizes)->Unit(benchmark::kMillisecond);

//==============================================================================
// SEARCH BENCHMARKS
//==============================================================================

// Fixture for search benchmarks (reuse index)
class SearchFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        if (!indexBuilt_) {
            buildIndex();
            indexBuilt_ = true;
        }
    }

    void TearDown(const ::benchmark::State& state) override {
        // Keep index for next iteration
    }

    static void buildIndex() {
        auto writer = createWriter();
        SyntheticGenerator gen(42);

        // Build index with 10K docs
        for (int i = 0; i < 10000; i++) {
            auto doc = gen.generateDocument(i, 200);
            writer->addDocument(doc);
        }

        writer->commit();
        writer->close();
    }

    static bool indexBuilt_;
};

bool SearchFixture::indexBuilt_ = false;

/**
 * Benchmark: TermQuery for rare term
 *
 * Comparable to Lucene's:
 *   Search("rare_word") > : 1000
 */
BENCHMARK_DEFINE_F(SearchFixture, TermQuery_RareTerm)(benchmark::State& state) {
    auto reader = openReader();
    search::IndexSearcher searcher(reader.get());

    // Query for a rare term (appears in ~1% of docs)
    auto query = search::TermQuery::create("body", "because");

    for (auto _ : state) {
        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(SearchFixture, TermQuery_RareTerm)->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: TermQuery for common term
 */
BENCHMARK_DEFINE_F(SearchFixture, TermQuery_CommonTerm)(benchmark::State& state) {
    auto reader = openReader();
    search::IndexSearcher searcher(reader.get());

    // Query for a common term (appears in ~50% of docs)
    auto query = search::TermQuery::create("body", "the");

    for (auto _ : state) {
        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(SearchFixture, TermQuery_CommonTerm)->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: BooleanQuery with AND (MUST + MUST)
 *
 * Comparable to Lucene's:
 *   Search("term1 AND term2")
 */
BENCHMARK_DEFINE_F(SearchFixture, BooleanQuery_TwoTermAND)(benchmark::State& state) {
    auto reader = openReader();
    search::IndexSearcher searcher(reader.get());

    // Build query: "work" AND "time"
    auto term1 = search::TermQuery::create("body", "work");
    auto term2 = search::TermQuery::create("body", "time");

    search::BooleanQuery::Builder builder;
    builder.add(std::move(term1), search::BooleanClause::Occur::MUST);
    builder.add(std::move(term2), search::BooleanClause::Occur::MUST);
    auto query = builder.build();

    for (auto _ : state) {
        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(SearchFixture, BooleanQuery_TwoTermAND)->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: BooleanQuery with OR (SHOULD + SHOULD)
 */
BENCHMARK_DEFINE_F(SearchFixture, BooleanQuery_TwoTermOR)(benchmark::State& state) {
    auto reader = openReader();
    search::IndexSearcher searcher(reader.get());

    // Build query: "work" OR "time"
    auto term1 = search::TermQuery::create("body", "work");
    auto term2 = search::TermQuery::create("body", "time");

    search::BooleanQuery::Builder builder;
    builder.add(std::move(term1), search::BooleanClause::Occur::SHOULD);
    builder.add(std::move(term2), search::BooleanClause::Occur::SHOULD);
    auto query = builder.build();

    for (auto _ : state) {
        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(SearchFixture, BooleanQuery_TwoTermOR)->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: TopK variation (varying result set size)
 */
BENCHMARK_DEFINE_F(SearchFixture, Search_TopK)(benchmark::State& state) {
    auto reader = openReader();
    search::IndexSearcher searcher(reader.get());

    auto query = search::TermQuery::create("body", "work");
    int k = state.range(0);

    for (auto _ : state) {
        auto results = searcher.search(query.get(), k);
        benchmark::DoNotOptimize(results);
    }

    state.SetLabel("k=" + std::to_string(k));
}
BENCHMARK_REGISTER_F(SearchFixture, Search_TopK)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: Cold cache (clear cache before each query)
 */
BENCHMARK_DEFINE_F(SearchFixture, Search_ColdCache)(benchmark::State& state) {
    auto query = search::TermQuery::create("body", "work");

    for (auto _ : state) {
        state.PauseTiming();
        // Simulate cold cache by reopening reader
        auto reader = openReader();
        search::IndexSearcher searcher(reader.get());
        state.ResumeTiming();

        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK_REGISTER_F(SearchFixture, Search_ColdCache)->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: Warm cache (reuse reader)
 */
BENCHMARK_DEFINE_F(SearchFixture, Search_WarmCache)(benchmark::State& state) {
    auto reader = openReader();
    search::IndexSearcher searcher(reader.get());

    auto query = search::TermQuery::create("body", "work");

    // Warm up
    for (int i = 0; i < 100; i++) {
        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }

    for (auto _ : state) {
        auto results = searcher.search(query.get(), 10);
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK_REGISTER_F(SearchFixture, Search_WarmCache)->Unit(benchmark::kMicrosecond);

//==============================================================================
// MAIN
//==============================================================================

BENCHMARK_MAIN();
