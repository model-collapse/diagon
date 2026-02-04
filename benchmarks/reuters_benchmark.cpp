/**
 * Reuters-21578 Benchmark
 *
 * Tests Diagon with the standard Reuters-21578 dataset used in Lucene benchmarks.
 * Enables direct comparison with Lucene's published results.
 */

#include "diagon/index/IndexWriter.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/document/Document.h"
#include "diagon/store/FSDirectory.h"
#include "dataset/ReutersDatasetAdapter.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <iomanip>

using namespace diagon;
using namespace std::chrono;

struct BenchmarkResult {
    int docsIndexed;
    int64_t indexTimeMs;
    double throughput;  // docs/sec
    int64_t indexSizeBytes;

    // Query results
    std::vector<std::pair<std::string, int64_t>> queryLatencies;  // query name, P99 latency in us
    std::vector<std::pair<std::string, int>> queryHits;            // query name, hit count
};

void printResults(const BenchmarkResult& result) {
    std::cout << "\n=========================================\n";
    std::cout << "Reuters-21578 Benchmark Results\n";
    std::cout << "=========================================\n\n";

    std::cout << "Indexing Performance:\n";
    std::cout << "  Documents: " << result.docsIndexed << "\n";
    std::cout << "  Time: " << (result.indexTimeMs / 1000.0) << " seconds\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << result.throughput << " docs/sec\n";
    std::cout << "  Index size: " << (result.indexSizeBytes / (1024 * 1024)) << " MB\n";
    std::cout << "  Storage: " << (result.indexSizeBytes / result.docsIndexed)
              << " bytes/doc\n\n";

    std::cout << "Search Performance (P99 latency):\n";
    for (size_t i = 0; i < result.queryLatencies.size(); i++) {
        std::cout << "  " << std::setw(20) << std::left << result.queryLatencies[i].first << ": "
                  << std::setw(8) << std::right << std::fixed << std::setprecision(3)
                  << (result.queryLatencies[i].second / 1000.0) << " ms";
        std::cout << "  (" << result.queryHits[i].second << " hits)\n";
    }

    std::cout << "\n=========================================\n";
}

int64_t getDirectorySize(const std::string& path) {
    std::string cmd = "du -sb " + path + " 2>/dev/null | cut -f1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return 0;

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);

    return result.empty() ? 0 : std::stoll(result);
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================\n";
    std::cout << "Diagon Reuters-21578 Benchmark\n";
    std::cout << "=========================================\n\n";

    // Default Reuters dataset path (Lucene format)
    std::string reutersPath = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";

    if (argc > 1) {
        reutersPath = argv[1];
    }

    std::cout << "Dataset path: " << reutersPath << "\n\n";

    BenchmarkResult result;

    // ========================================
    // Phase 1: Indexing
    // ========================================
    std::cout << "Phase 1: Indexing Reuters-21578 documents\n";
    std::cout << "========================================\n";

    std::string indexPath = "/tmp/diagon_reuters_index";
    [[maybe_unused]] int ret1 = system(("rm -rf " + indexPath).c_str());
    [[maybe_unused]] int ret2 = system(("mkdir -p " + indexPath).c_str());

    auto indexStart = high_resolution_clock::now();

    try {
        auto dir = store::FSDirectory::open(indexPath);
        index::IndexWriterConfig config;
        config.setMaxBufferedDocs(50000); // Single segment for Reuters

        auto writer = std::make_unique<index::IndexWriter>(*dir, config);

        // Read Reuters dataset
        benchmarks::ReutersDatasetAdapter adapter(reutersPath);

        int docCount = 0;
        document::Document doc;

        std::cout << "Reading documents...\n";
        while (adapter.nextDocument(doc)) {
            writer->addDocument(doc);
            docCount++;

            if (docCount % 1000 == 0) {
                std::cout << "  Indexed " << docCount << " documents\r" << std::flush;
            }

            // Clear doc for reuse
            doc = document::Document();
        }

        std::cout << "\nCommitting index...\n";
        writer->commit();
        writer.reset();

        result.docsIndexed = docCount;
        std::cout << "✓ Indexed " << docCount << " documents\n";

    } catch (const std::exception& e) {
        std::cerr << "Error during indexing: " << e.what() << "\n";
        return 1;
    }

    auto indexEnd = high_resolution_clock::now();
    result.indexTimeMs = duration_cast<milliseconds>(indexEnd - indexStart).count();
    result.throughput = (result.docsIndexed * 1000.0) / result.indexTimeMs;
    result.indexSizeBytes = getDirectorySize(indexPath);

    std::cout << "✓ Indexing complete in " << (result.indexTimeMs / 1000.0) << " seconds\n";
    std::cout << "✓ Throughput: " << std::fixed << std::setprecision(0)
              << result.throughput << " docs/sec\n\n";

    // ========================================
    // Phase 2: Search Queries
    // ========================================
    std::cout << "Phase 2: Search performance\n";
    std::cout << "========================================\n";

    try {
        auto dir = store::FSDirectory::open(indexPath);
        auto reader = index::DirectoryReader::open(*dir);
        auto searcher = std::make_unique<search::IndexSearcher>(*reader);

        // Define test queries (typical Reuters queries)
        struct TestQuery {
            std::string name;
            std::function<std::unique_ptr<search::Query>()> builder;
        };

        std::vector<TestQuery> queries = {
            {"Single term: 'dollar'", []() {
                return std::make_unique<search::TermQuery>(search::Term("body", "dollar"));
            }},
            {"Single term: 'oil'", []() {
                return std::make_unique<search::TermQuery>(search::Term("body", "oil"));
            }},
            {"Single term: 'trade'", []() {
                return std::make_unique<search::TermQuery>(search::Term("body", "trade"));
            }},
            {"Boolean: 'oil AND price'", []() {
                search::BooleanQuery::Builder builder;
                builder.add(std::make_shared<search::TermQuery>(search::Term("body", "oil")),
                           search::Occur::MUST);
                builder.add(std::make_shared<search::TermQuery>(search::Term("body", "price")),
                           search::Occur::MUST);
                return builder.build();
            }},
            {"Boolean: 'trade OR export'", []() {
                search::BooleanQuery::Builder builder;
                builder.add(std::make_shared<search::TermQuery>(search::Term("body", "trade")),
                           search::Occur::SHOULD);
                builder.add(std::make_shared<search::TermQuery>(search::Term("body", "export")),
                           search::Occur::SHOULD);
                return builder.build();
            }}
        };

        const int NUM_ITERATIONS = 100;
        const int WARMUP_ITERATIONS = 10;

        for (auto& testQuery : queries) {
            std::cout << "\nTesting: " << testQuery.name << "\n";

            // Warmup
            for (int i = 0; i < WARMUP_ITERATIONS; i++) {
                auto query = testQuery.builder();
                auto topDocs = searcher->search(*query, 100);
            }

            // Benchmark
            std::vector<int64_t> latencies;
            latencies.reserve(NUM_ITERATIONS);
            int hits = 0;

            for (int i = 0; i < NUM_ITERATIONS; i++) {
                auto query = testQuery.builder();

                auto start = high_resolution_clock::now();
                auto topDocs = searcher->search(*query, 100);
                auto end = high_resolution_clock::now();

                latencies.push_back(duration_cast<microseconds>(end - start).count());

                if (i == 0) {
                    hits = topDocs.totalHits.value;
                }
            }

            // Calculate P99
            std::sort(latencies.begin(), latencies.end());
            int64_t p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];

            result.queryLatencies.push_back({testQuery.name, p99});
            result.queryHits.push_back({testQuery.name, hits});

            std::cout << "  P99: " << std::fixed << std::setprecision(3) << (p99 / 1000.0) << " ms"
                     << "  (" << hits << " hits)\n";
        }

        reader.reset();

    } catch (const std::exception& e) {
        std::cerr << "Error during search: " << e.what() << "\n";
        return 1;
    }

    // Print final results
    printResults(result);

    // Save results to file for comparison
    std::ofstream outFile("reuters_benchmark_results.txt");
    if (outFile.is_open()) {
        outFile << "Documents: " << result.docsIndexed << "\n";
        outFile << "Indexing time (ms): " << result.indexTimeMs << "\n";
        outFile << "Throughput (docs/sec): " << result.throughput << "\n";
        outFile << "Index size (bytes): " << result.indexSizeBytes << "\n";
        for (size_t i = 0; i < result.queryLatencies.size(); i++) {
            outFile << "Query: " << result.queryLatencies[i].first
                   << " | P99 (us): " << result.queryLatencies[i].second
                   << " | Hits: " << result.queryHits[i].second << "\n";
        }
        outFile.close();
        std::cout << "\n✓ Results saved to reuters_benchmark_results.txt\n";
    }

    return 0;
}
