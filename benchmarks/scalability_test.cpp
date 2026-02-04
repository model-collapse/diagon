/**
 * Production-Scale Scalability Test
 * Tests Diagon with incrementally larger datasets to identify scalability limits
 */

#include "diagon/index/IndexWriter.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/Term.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace diagon;
using namespace std::chrono;

struct ScalabilityResult {
    int numDocs;
    int64_t indexTimeMs;
    double throughput;  // docs/sec
    int64_t indexSizeBytes;
    int64_t peakMemoryMB;
    int64_t searchP99Us;
    int searchHits;
};

// Get RSS (Resident Set Size) memory usage in MB
int64_t getCurrentMemoryMB() {
    std::ifstream statm("/proc/self/statm");
    long pages;
    statm >> pages;  // First field is total program size in pages
    statm.close();

    long pageSize = sysconf(_SC_PAGESIZE);
    return (pages * pageSize) / (1024 * 1024);  // Convert to MB
}

// Get directory size in bytes
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

    return std::stoll(result);
}

ScalabilityResult runScalabilityTest(int numDocs) {
    ScalabilityResult result;
    result.numDocs = numDocs;

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Testing with " << numDocs << " documents" << std::endl;
    std::cout << "=========================================" << std::endl;

    // Indexing phase
    std::cout << "Phase 1: Indexing..." << std::endl;

    std::string indexPath = "/tmp/diagon_scalability_index";
    [[maybe_unused]] int ret1 = system(("rm -rf " + indexPath).c_str());
    [[maybe_unused]] int ret2 = system(("mkdir -p " + indexPath).c_str());

    auto indexStart = high_resolution_clock::now();
    int64_t memoryBefore = getCurrentMemoryMB();
    int64_t peakMemory = memoryBefore;

    auto dir = store::FSDirectory::open(indexPath);
    index::IndexWriterConfig config;
    config.setMaxBufferedDocs(std::max(100000, numDocs + 1000));  // Single segment

    auto writer = std::make_unique<index::IndexWriter>(*dir, config);

    // Generate documents with realistic variety
    const std::vector<std::string> terms = {
        "error", "warning", "info", "critical", "debug",
        "success", "failure", "timeout", "connection", "database",
        "user", "system", "network", "security", "performance",
        "cache", "query", "response", "request", "latency"
    };

    const int MEMORY_CHECK_INTERVAL = 1000;

    for (int i = 0; i < numDocs; i++) {
        document::Document doc;

        // Create message with varied term combinations
        std::ostringstream msgStream;
        msgStream << "Log entry " << i << " ";

        // Add 3-5 terms per document
        int numTerms = 3 + (i % 3);
        for (int t = 0; t < numTerms; t++) {
            int termIdx = (i * 7 + t * 11) % terms.size();
            msgStream << terms[termIdx] << " ";
        }

        doc.add(std::make_unique<document::TextField>("message", msgStream.str()));
        doc.add(std::make_unique<document::StringField>("id", std::to_string(i)));

        writer->addDocument(doc);

        // Track peak memory periodically
        if (i > 0 && i % MEMORY_CHECK_INTERVAL == 0) {
            int64_t currentMemory = getCurrentMemoryMB();
            peakMemory = std::max(peakMemory, currentMemory);

            // Progress indicator
            if (numDocs >= 100000 && i % (numDocs / 10) == 0) {
                double progress = 100.0 * i / numDocs;
                std::cout << "  Progress: " << std::fixed << std::setprecision(1)
                         << progress << "% (" << i << "/" << numDocs << " docs)" << std::endl;
            }
        }
    }

    std::cout << "  Committing..." << std::endl;
    writer->commit();
    writer.reset();

    auto indexEnd = high_resolution_clock::now();
    result.indexTimeMs = duration_cast<milliseconds>(indexEnd - indexStart).count();
    result.throughput = (numDocs * 1000.0) / result.indexTimeMs;
    result.peakMemoryMB = peakMemory;
    result.indexSizeBytes = getDirectorySize(indexPath);

    std::cout << "✓ Indexing complete in " << result.indexTimeMs << " ms" << std::endl;
    std::cout << "✓ Throughput: " << std::fixed << std::setprecision(0)
             << result.throughput << " docs/sec" << std::endl;
    std::cout << "✓ Index size: " << (result.indexSizeBytes / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "✓ Peak memory: " << result.peakMemoryMB << " MB" << std::endl;

    // Search phase
    std::cout << "\nPhase 2: Search performance..." << std::endl;

    auto reader = index::DirectoryReader::open(*dir);
    auto searcher = std::make_unique<search::IndexSearcher>(*reader);

    // Warmup
    for (int i = 0; i < 10; i++) {
        auto termQuery = std::make_unique<search::TermQuery>(search::Term("message", "error"));
        auto results = searcher->search(*termQuery, 100);
    }

    // Benchmark query
    const int NUM_ITERATIONS = 100;
    std::vector<int64_t> latencies;
    latencies.reserve(NUM_ITERATIONS);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        // Create query using Builder pattern
        search::BooleanQuery::Builder queryBuilder;
        queryBuilder.add(std::make_shared<search::TermQuery>(search::Term("message", "error")),
                        search::Occur::MUST);
        queryBuilder.add(std::make_shared<search::TermQuery>(search::Term("message", "warning")),
                        search::Occur::MUST);
        auto query = queryBuilder.build();

        auto start = high_resolution_clock::now();
        auto results = searcher->search(*query, 1000);
        auto end = high_resolution_clock::now();

        latencies.push_back(duration_cast<microseconds>(end - start).count());

        if (i == 0) {
            result.searchHits = results.totalHits.value;
        }
    }

    // Calculate P99
    std::sort(latencies.begin(), latencies.end());
    result.searchP99Us = latencies[static_cast<size_t>(latencies.size() * 0.99)];

    std::cout << "✓ Search P99: " << (result.searchP99Us / 1000.0) << " ms" << std::endl;
    std::cout << "✓ Query hits: " << result.searchHits << " documents" << std::endl;

    reader.reset();

    return result;
}

void printSummaryTable(const std::vector<ScalabilityResult>& results) {
    std::cout << "\n=========================================" << std::endl;
    std::cout << "Scalability Test Summary" << std::endl;
    std::cout << "=========================================" << std::endl << std::endl;

    std::cout << "| Documents | Index Time | Throughput | Index Size | Peak Mem | Search P99 |" << std::endl;
    std::cout << "|-----------|------------|------------|------------|----------|------------|" << std::endl;

    for (const auto& r : results) {
        std::cout << "| " << std::setw(9) << r.numDocs << " | ";
        std::cout << std::setw(8) << (r.indexTimeMs / 1000.0) << "s | ";
        std::cout << std::setw(8) << std::fixed << std::setprecision(0) << r.throughput << " d/s | ";
        std::cout << std::setw(8) << (r.indexSizeBytes / (1024 * 1024)) << " MB | ";
        std::cout << std::setw(6) << r.peakMemoryMB << " MB | ";
        std::cout << std::setw(8) << std::fixed << std::setprecision(3) << (r.searchP99Us / 1000.0) << " ms |" << std::endl;
    }

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Scalability Analysis" << std::endl;
    std::cout << "=========================================" << std::endl << std::endl;

    if (results.size() >= 2) {
        // Check if throughput scales linearly
        double firstThroughput = results[0].throughput;
        double lastThroughput = results.back().throughput;
        double throughputChange = ((lastThroughput - firstThroughput) / firstThroughput) * 100.0;

        std::cout << "Throughput scaling: ";
        if (std::abs(throughputChange) < 10.0) {
            std::cout << "✅ LINEAR (within 10%)" << std::endl;
        } else if (throughputChange < -20.0) {
            std::cout << "⚠️  DEGRADED (" << std::fixed << std::setprecision(1)
                     << throughputChange << "%)" << std::endl;
        } else {
            std::cout << "Change: " << std::fixed << std::setprecision(1)
                     << throughputChange << "%" << std::endl;
        }

        // Check search latency scaling
        double firstSearch = results[0].searchP99Us / 1000.0;
        double lastSearch = results.back().searchP99Us / 1000.0;
        double searchChange = ((lastSearch - firstSearch) / firstSearch) * 100.0;

        std::cout << "Search P99 scaling: ";
        if (searchChange < 50.0) {
            std::cout << "✅ SUB-LINEAR (+";
        } else if (searchChange < 100.0) {
            std::cout << "⚠️  LINEAR (+";
        } else {
            std::cout << "❌ SUPER-LINEAR (+";
        }
        std::cout << std::fixed << std::setprecision(1) << searchChange << "%)" << std::endl;

        // Memory efficiency
        double memoryPerDoc = static_cast<double>(results.back().peakMemoryMB) / (results.back().numDocs / 1000.0);
        std::cout << "Memory efficiency: " << std::fixed << std::setprecision(2)
                 << memoryPerDoc << " MB per 1K docs" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================" << std::endl;
    std::cout << "Diagon Production-Scale Scalability Test" << std::endl;
    std::cout << "=========================================" << std::endl;

    // Test with progressively larger datasets
    std::vector<int> testSizes = {10000, 100000, 500000, 1000000};

    // Allow custom test sizes via command line
    if (argc > 1) {
        testSizes.clear();
        for (int i = 1; i < argc; i++) {
            testSizes.push_back(std::stoi(argv[i]));
        }
    }

    std::vector<ScalabilityResult> results;

    for (int numDocs : testSizes) {
        try {
            auto result = runScalabilityTest(numDocs);
            results.push_back(result);
        } catch (const std::exception& e) {
            std::cerr << "❌ Test failed for " << numDocs << " docs: " << e.what() << std::endl;
            break;
        }
    }

    if (!results.empty()) {
        printSummaryTable(results);
    }

    return 0;
}
