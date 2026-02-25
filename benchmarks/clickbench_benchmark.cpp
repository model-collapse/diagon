/**
 * ClickBench Benchmark
 *
 * Benchmarks Diagon on analytical queries derived from ClickBench (Yandex.Metrica
 * web analytics, 100M rows). Tests numeric range queries, boolean filtering,
 * counting at scale, and text search on URLs/keywords.
 *
 * 15 queries covering: COUNT(*), NOT filter, point lookup, text search,
 * multi-filter AND, complex boolean, multi-term OR, numeric range.
 *
 * Usage:
 *   ./ClickBenchBenchmark [--data-path PATH] [--max-docs N] [--index-path PATH]
 */

#include "diagon/document/Document.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/MatchAllDocsQuery.h"
#include "diagon/search/NumericRangeQuery.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"

#include "columnar/ColumnarStore.h"
#include "dataset/ClickBenchAdapter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace diagon;
using namespace std::chrono;

// ========================================
// Structures (same as reuters_benchmark.cpp)
// ========================================

struct QueryMetrics {
    std::string name;
    int hits;
    int64_t p50_us;
    int64_t p90_us;
    int64_t p99_us;
};

struct BenchmarkResult {
    int docsIndexed;
    int64_t indexTimeMs;
    double throughput;  // docs/sec
    int64_t indexSizeBytes;

    std::vector<QueryMetrics> queryResults;
};

// ========================================
// Helpers
// ========================================

void printResults(const BenchmarkResult& result) {
    std::cout << "\n=========================================\n";
    std::cout << "ClickBench Benchmark Results\n";
    std::cout << "=========================================\n\n";

    std::cout << "Indexing Performance:\n";
    std::cout << "  Documents: " << result.docsIndexed << "\n";
    std::cout << "  Time: " << (result.indexTimeMs / 1000.0) << " seconds\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << result.throughput
              << " docs/sec\n";
    std::cout << "  Index size: " << (result.indexSizeBytes / (1024 * 1024)) << " MB\n";
    if (result.docsIndexed > 0) {
        std::cout << "  Storage: " << (result.indexSizeBytes / result.docsIndexed) << " bytes/doc\n";
    }
    std::cout << "\n";

    std::cout << "Search Performance (P50 / P90 / P99 latency):\n";
    std::cout << std::setw(55) << std::left << "  Query" << std::setw(12) << std::right
              << "P50 (ms)" << std::setw(12) << "P90 (ms)" << std::setw(12) << "P99 (ms)"
              << std::setw(12) << "Hits"
              << "\n";
    std::cout << "  " << std::string(99, '-') << "\n";
    for (const auto& qm : result.queryResults) {
        std::cout << "  " << std::setw(53) << std::left << qm.name << std::setw(12) << std::right
                  << std::fixed << std::setprecision(3) << (qm.p50_us / 1000.0) << std::setw(12)
                  << (qm.p90_us / 1000.0) << std::setw(12) << (qm.p99_us / 1000.0) << std::setw(12)
                  << qm.hits << "\n";
    }

    std::cout << "\n=========================================\n";
}

int64_t getDirectorySize(const std::string& path) {
    std::string cmd = "du -sb " + path + " 2>/dev/null | cut -f1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return 0;

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    return result.empty() ? 0 : std::stoll(result);
}

// ========================================
// CLI Argument Parsing
// ========================================

struct CliArgs {
    std::string dataPath = "/home/ubuntu/data/clickbench/hits.tsv";
    int maxDocs = 10000000;
    std::string indexPath = "/tmp/diagon_clickbench_index";
};

CliArgs parseArgs(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--data-path") == 0 && i + 1 < argc) {
            args.dataPath = argv[++i];
        } else if (std::strcmp(argv[i], "--max-docs") == 0 && i + 1 < argc) {
            args.maxDocs = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--index-path") == 0 && i + 1 < argc) {
            args.indexPath = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: ClickBenchBenchmark [options]\n"
                      << "  --data-path PATH   Path to hits.tsv (default: "
                      << args.dataPath << ")\n"
                      << "  --max-docs N       Max documents to index (default: "
                      << args.maxDocs << ")\n"
                      << "  --index-path PATH  Index directory (default: "
                      << args.indexPath << ")\n";
            std::exit(0);
        }
    }
    return args;
}

// ========================================
// Main
// ========================================

int main(int argc, char* argv[]) {
    CliArgs args = parseArgs(argc, argv);

    std::cout << "=========================================\n";
    std::cout << "Diagon ClickBench Benchmark\n";
    std::cout << "=========================================\n\n";
    std::cout << "Dataset path: " << args.dataPath << "\n";
    std::cout << "Max documents: " << args.maxDocs << "\n";
    std::cout << "Index path: " << args.indexPath << "\n\n";

    BenchmarkResult result;

    // ========================================
    // Phase 1: Indexing
    // ========================================
    std::cout << "Phase 1: Indexing ClickBench documents\n";
    std::cout << "========================================\n";

    // Clean index directory
    [[maybe_unused]] int ret1 = system(("rm -rf " + args.indexPath).c_str());
    [[maybe_unused]] int ret2 = system(("mkdir -p " + args.indexPath).c_str());

    auto indexStart = high_resolution_clock::now();

    // Columnar store path for numeric range queries (Q9, Q10, Q14)
    std::string colPath = args.indexPath + "_columnar";

    try {
        auto dir = store::FSDirectory::open(args.indexPath);
        index::IndexWriterConfig config;
        // Set maxBufferedDocs to match actual doc count to produce a single segment.
        // The NumericDocValuesWriter allocates a dense array sized to maxBufferedDocs,
        // so setting this too high wastes massive disk/RAM (20M * 8 fields * 8 bytes = 1.2GB).
        config.setMaxBufferedDocs(args.maxDocs + 1000);

        auto writer = std::make_unique<index::IndexWriter>(*dir, config);

        // Create columnar store for numeric range columns
        columnar::ColumnarWriter colWriter(colPath);
        colWriter.defineColumn("RegionID");
        colWriter.defineColumn("ResolutionWidth");
        colWriter.defineColumn("CounterID");

        benchmarks::ClickBenchAdapter adapter(args.dataPath, args.maxDocs);

        int docCount = 0;
        document::Document doc;

        std::cout << "Reading documents...\n";
        while (adapter.nextDocument(doc)) {
            writer->addDocument(doc);

            // Feed numeric values to columnar store
            const auto& numericVals = adapter.getLastNumericValues();
            for (const auto& colName : {"RegionID", "ResolutionWidth", "CounterID"}) {
                auto it = numericVals.find(colName);
                if (it != numericVals.end()) {
                    colWriter.addValue(colName, it->second);
                } else {
                    // Missing value: write 0 as sentinel (field was empty/non-numeric)
                    colWriter.addValue(colName, 0);
                }
            }
            colWriter.endDocument();

            docCount++;

            if (docCount % 100000 == 0) {
                std::cout << "  Indexed " << docCount << " documents\r" << std::flush;
            }

            // Clear doc for reuse
            doc = document::Document();
        }

        std::cout << "\nCommitting index...\n";
        writer->commit();
        writer.reset();
        colWriter.close();

        result.docsIndexed = docCount;
        std::cout << "Indexed " << docCount << " documents\n";
        std::cout << "Columnar store written to: " << colPath << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error during indexing: " << e.what() << "\n";
        return 1;
    }

    auto indexEnd = high_resolution_clock::now();
    result.indexTimeMs = duration_cast<milliseconds>(indexEnd - indexStart).count();
    result.throughput = (result.indexTimeMs > 0)
                            ? (result.docsIndexed * 1000.0) / result.indexTimeMs
                            : 0;
    result.indexSizeBytes = getDirectorySize(args.indexPath);

    std::cout << "Indexing complete in " << (result.indexTimeMs / 1000.0) << " seconds\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) << result.throughput
              << " docs/sec\n";
    std::cout << "Index size: " << (result.indexSizeBytes / (1024 * 1024)) << " MB\n\n";

    // ========================================
    // Phase 2: Search Queries
    // ========================================
    std::cout << "Phase 2: Search performance (15 queries)\n";
    std::cout << "========================================\n";

    try {
        // MMapDirectory for zero-copy reads (MANDATORY per CLAUDE.md)
        auto dir = store::MMapDirectory::open(args.indexPath);
        auto reader = index::DirectoryReader::open(*dir);
        auto searcher = std::make_unique<search::IndexSearcher>(*reader);

        // Open columnar readers for range query columns (Q9, Q10, Q14)
        columnar::ColumnarReader regionReader, widthReader, counterReader;
        regionReader.open(colPath, "RegionID");
        widthReader.open(colPath, "ResolutionWidth");
        counterReader.open(colPath, "CounterID");

        std::cout << "Columnar store: " << regionReader.granulesTotal()
                  << " granules per column (" << regionReader.totalDocs() << " docs)\n";

        // ---- Lucene queries (Q1-Q8, Q11-Q13, Q15) ----
        struct TestQuery {
            std::string name;
            std::function<std::unique_ptr<search::Query>()> builder;
        };

        std::vector<TestQuery> luceneQueries = {
            // Q1: COUNT(*) — MatchAllDocsQuery full scan baseline
            {"Q1  COUNT(*)",
             []() {
                 return std::make_unique<search::MatchAllQuery>();
             }},

            // Q2: WHERE AdvEngineID <> 0 — NOT filter
            {"Q2  AdvEngineID <> 0",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::MatchAllQuery>(),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("AdvEngineID_s", "0")),
                             search::Occur::MUST_NOT);
                 return builder.build();
             }},

            // Q3: WHERE UserID = specific value — point lookup
            {"Q3  UserID = 435090932899640449",
             []() {
                 return std::make_unique<search::TermQuery>(
                     search::Term("UserID_s", "435090932899640449"));
             }},

            // Q4: WHERE URL LIKE '%google%' — text search (tokenized)
            {"Q4  URL contains 'google'",
             []() {
                 return std::make_unique<search::TermQuery>(
                     search::Term("URL", "google"));
             }},

            // Q5: CounterID=62 AND EventDate range AND flags — multi-filter AND
            {"Q5  CounterID=62 AND date AND flags",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("CounterID_s", "62")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("EventDate", "2013-07-15")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("IsRefresh", "0")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("DontCountHits", "0")),
                             search::Occur::MUST);
                 return builder.build();
             }},

            // Q6: Same pattern, different date — multi-filter AND
            {"Q6  CounterID=62 AND date=2013-07-01",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("CounterID_s", "62")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("EventDate", "2013-07-01")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("IsRefresh", "0")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("DontCountHits", "0")),
                             search::Occur::MUST);
                 return builder.build();
             }},

            // Q7: Complex boolean — 6 MUST/MUST_NOT clauses
            {"Q7  Complex: CID=62 AND flags (6 clauses)",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("CounterID_s", "62")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("EventDate", "2013-07-15")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("IsRefresh", "0")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("DontCountHits", "0")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("IsDownload", "0")),
                             search::Occur::MUST);
                 // IsLink <> 0 — MUST_NOT on "0" won't work alone, use MatchAll+MUST_NOT
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("IsLink", "0")),
                             search::Occur::MUST_NOT);
                 return builder.build();
             }},

            // Q8: RegionID IN (1..10) — multi-term OR on StringField
            {"Q8  RegionID IN (1..10)",
             []() {
                 search::BooleanQuery::Builder builder;
                 for (int i = 1; i <= 10; i++) {
                     builder.add(std::make_shared<search::TermQuery>(
                                     search::Term("RegionID_s", std::to_string(i))),
                                 search::Occur::SHOULD);
                 }
                 return builder.build();
             }},

            // Q11: URL contains 'google' AND AdvEngineID <> 0 — text + numeric filter
            {"Q11 URL='google' AND AdvEngineID<>0",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("URL", "google")),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("AdvEngineID_s", "0")),
                             search::Occur::MUST_NOT);
                 return builder.build();
             }},

            // Q12: SearchPhrase <> '' — inverted filter (non-empty)
            {"Q12 SearchPhrase <> '' (non-empty)",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::MatchAllQuery>(),
                             search::Occur::MUST);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("SearchPhrase_s", "")),
                             search::Occur::MUST_NOT);
                 return builder.build();
             }},

            // Q13: EventDate = '2013-07-15' — date exact match
            {"Q13 EventDate = '2013-07-15'",
             []() {
                 return std::make_unique<search::TermQuery>(
                     search::Term("EventDate", "2013-07-15"));
             }},

            // Q15: AdvEngineID=2 OR 3 OR 4 — small OR
            {"Q15 AdvEngineID IN (2,3,4)",
             []() {
                 search::BooleanQuery::Builder builder;
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("AdvEngineID_s", "2")),
                             search::Occur::SHOULD);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("AdvEngineID_s", "3")),
                             search::Occur::SHOULD);
                 builder.add(std::make_shared<search::TermQuery>(
                                 search::Term("AdvEngineID_s", "4")),
                             search::Occur::SHOULD);
                 return builder.build();
             }},
        };

        // ---- Columnar range queries (Q9, Q10, Q14) ----
        struct ColumnarQuery {
            std::string name;
            columnar::ColumnarReader* reader;
            int64_t lower;
            int64_t upper;
            bool includeLower;
            bool includeUpper;
        };

        std::vector<ColumnarQuery> columnarQueries = {
            // Q9: RegionID BETWEEN 200 AND 300
            {"Q9  RegionID BETWEEN 200 AND 300 [COLUMNAR]",
             &regionReader, 200, 300, true, true},

            // Q10: ResolutionWidth >= 1900 (open upper bound)
            {"Q10 ResolutionWidth >= 1900 [COLUMNAR]",
             &widthReader, 1900, std::numeric_limits<int64_t>::max(), true, true},

            // Q14: CounterID BETWEEN 0 AND 100
            {"Q14 CounterID BETWEEN 0 AND 100 [COLUMNAR]",
             &counterReader, 0, 100, true, true},
        };

        const int NUM_ITERATIONS = 100;
        const int WARMUP_ITERATIONS = 10;

        // ---- Benchmark Lucene queries ----
        for (auto& testQuery : luceneQueries) {
            std::cout << "\nTesting: " << testQuery.name << "\n";

            // Warmup
            for (int i = 0; i < WARMUP_ITERATIONS; i++) {
                auto query = testQuery.builder();
                auto topDocs = searcher->search(*query, 10);
            }

            // Benchmark
            std::vector<int64_t> latencies;
            latencies.reserve(NUM_ITERATIONS);
            int hits = 0;

            for (int i = 0; i < NUM_ITERATIONS; i++) {
                auto query = testQuery.builder();

                auto start = high_resolution_clock::now();
                auto topDocs = searcher->search(*query, 10);
                auto end = high_resolution_clock::now();

                latencies.push_back(duration_cast<microseconds>(end - start).count());

                if (i == 0) {
                    hits = static_cast<int>(topDocs.totalHits.value);
                }
            }

            // Calculate P50/P90/P99
            std::sort(latencies.begin(), latencies.end());
            int64_t p50 = latencies[static_cast<size_t>(latencies.size() * 0.50)];
            int64_t p90 = latencies[static_cast<size_t>(latencies.size() * 0.90)];
            int64_t p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];

            result.queryResults.push_back({testQuery.name, hits, p50, p90, p99});

            std::cout << "  P50: " << std::fixed << std::setprecision(3) << (p50 / 1000.0)
                      << " ms  P90: " << (p90 / 1000.0) << " ms  P99: " << (p99 / 1000.0)
                      << " ms  (" << hits << " hits)\n";
        }

        // ---- Benchmark Columnar range queries ----
        for (auto& cq : columnarQueries) {
            std::cout << "\nTesting: " << cq.name << "\n";

            // Warmup
            for (int i = 0; i < WARMUP_ITERATIONS; i++) {
                cq.reader->rangeCount(cq.lower, cq.upper, cq.includeLower, cq.includeUpper);
            }

            // Benchmark
            std::vector<int64_t> latencies;
            latencies.reserve(NUM_ITERATIONS);
            int hits = 0;

            for (int i = 0; i < NUM_ITERATIONS; i++) {
                auto start = high_resolution_clock::now();
                int count = cq.reader->rangeCount(cq.lower, cq.upper,
                                                  cq.includeLower, cq.includeUpper);
                auto end = high_resolution_clock::now();

                latencies.push_back(duration_cast<microseconds>(end - start).count());

                if (i == 0) {
                    hits = count;
                }
            }

            // Calculate P50/P90/P99
            std::sort(latencies.begin(), latencies.end());
            int64_t p50 = latencies[static_cast<size_t>(latencies.size() * 0.50)];
            int64_t p90 = latencies[static_cast<size_t>(latencies.size() * 0.90)];
            int64_t p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];

            result.queryResults.push_back({cq.name, hits, p50, p90, p99});

            std::cout << "  " << cq.reader->granulesTotal() << " granules, "
                      << cq.reader->granulesScanned() << " scanned, "
                      << cq.reader->granulesSkipped() << " skipped, "
                      << cq.reader->granulesBulkCounted() << " bulk-counted\n";
            std::cout << "  P50: " << std::fixed << std::setprecision(3) << (p50 / 1000.0)
                      << " ms  P90: " << (p90 / 1000.0) << " ms  P99: " << (p99 / 1000.0)
                      << " ms  (" << hits << " hits)\n";
        }

        reader.reset();

    } catch (const std::exception& e) {
        std::cerr << "Error during search: " << e.what() << "\n";
        return 1;
    }

    // Print final results
    printResults(result);

    // Save results to file
    std::ofstream outFile("clickbench_benchmark_results.txt");
    if (outFile.is_open()) {
        outFile << "Documents: " << result.docsIndexed << "\n";
        outFile << "Indexing time (ms): " << result.indexTimeMs << "\n";
        outFile << "Throughput (docs/sec): " << result.throughput << "\n";
        outFile << "Index size (bytes): " << result.indexSizeBytes << "\n";
        for (const auto& qm : result.queryResults) {
            outFile << "Query: " << qm.name << " | P50 (us): " << qm.p50_us
                    << " | P90 (us): " << qm.p90_us << " | P99 (us): " << qm.p99_us
                    << " | Hits: " << qm.hits << "\n";
        }
        outFile.close();
        std::cout << "\nResults saved to clickbench_benchmark_results.txt\n";
    }

    return 0;
}
