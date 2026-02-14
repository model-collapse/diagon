// Standalone profiler for callgrind — no Google Benchmark overhead
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace diagon;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    int numTerms = 5;
    int iterations = 10000;
    if (argc > 1)
        numTerms = std::atoi(argv[1]);
    if (argc > 2)
        iterations = std::atoi(argv[2]);

    fs::path indexPath = "/tmp/diagon_reuters_index";
    if (!fs::exists(indexPath)) {
        std::cerr << "Index not found at " << indexPath << std::endl;
        return 1;
    }

    auto dir = store::MMapDirectory::open(indexPath.string());
    auto reader = index::DirectoryReader::open(*dir);

    search::IndexSearcherConfig config;
    config.enable_block_max_wand = true;
    search::IndexSearcher searcher(*reader, config);

    static const std::vector<std::string> queryTerms = {
        "market",   "company",  "stock",      "trade",    "price",      "bank",    "dollar",
        "oil",      "export",   "government", "share",    "billion",    "profit",  "exchange",
        "interest", "economic", "report",     "industry", "investment", "revenue", "million",
        "percent",  "year",     "said",       "would",    "new",        "also",    "last",
        "first",    "group",    "accord",     "tax",      "rate",       "growth",  "debt",
        "loss",     "quarter",  "month",      "net",      "income",     "sales",   "earnings",
        "bond",     "foreign",  "loan",       "budget",   "deficit",    "surplus", "inflation",
        "central"};

    search::BooleanQuery::Builder builder;
    for (int i = 0; i < numTerms && i < static_cast<int>(queryTerms.size()); i++) {
        builder.add(std::make_shared<search::TermQuery>(search::Term("body", queryTerms[i])),
                    search::Occur::SHOULD);
    }
    auto query = builder.build();

    // Warmup
    for (int i = 0; i < 100; i++) {
        auto results = searcher.search(*query, 10);
    }

    // Timed loop
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        auto results = searcher.search(*query, 10);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count() / iterations;
    std::cout << "OR-" << numTerms << " WAND: " << us << " us/query (" << iterations
              << " iterations)" << std::endl;
    return 0;
}
