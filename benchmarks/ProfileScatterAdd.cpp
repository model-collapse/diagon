// Micro-profiling tool for scatter-add phases
// Uses RDTSC for cycle-accurate timing of hot code paths

#include "diagon/index/BlockMaxQuantizedIndex.h"

#include "profile_helper.h"

#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using namespace diagon::index;

// Load MSMarco queries
std::vector<SparseDoc> loadQueries(const std::string& path, size_t max_queries) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open queries file: " + path);
    }

    std::vector<SparseDoc> queries;
    std::string line;

    while (std::getline(file, line) && queries.size() < max_queries) {
        SparseDoc query;
        std::istringstream iss(line);
        std::string token;

        while (iss >> token) {
            size_t colon_pos = token.find(':');
            if (colon_pos != std::string::npos) {
                term_t term = std::stoi(token.substr(0, colon_pos));
                float score = std::stof(token.substr(colon_pos + 1));
                query.emplace_back(term, score);
            }
        }

        if (!query.empty()) {
            queries.push_back(std::move(query));
        }
    }

    return queries;
}

int main(int argc, char* argv[]) {
    std::string index_path = "/home/ubuntu/msmarco/full_data/queries.dev.txt";
    size_t num_queries = 100;
    float alpha = 0.3f;
    double cpu_freq_ghz = 2.5;  // Default, adjust for your CPU

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--queries" && i + 1 < argc) {
            num_queries = std::stoul(argv[++i]);
        } else if (arg == "--alpha" && i + 1 < argc) {
            alpha = std::stof(argv[++i]);
        } else if (arg == "--cpu-freq" && i + 1 < argc) {
            cpu_freq_ghz = std::stod(argv[++i]);
        }
    }

    std::cout << "Micro-profiling BlockMaxQuantizedIndex scatter-add..." << std::endl;
    std::cout << "Queries: " << num_queries << ", Alpha: " << alpha << std::endl;
    std::cout << "CPU Frequency: " << cpu_freq_ghz << " GHz (override with --cpu-freq)"
              << std::endl;
    std::cout << std::endl;

    // Load index
    std::cout << "Loading index..." << std::endl;
    BlockMaxQuantizedIndex::Config config;
    config.window_size = 500000;
    config.window_group_size = 15;
    config.use_custom_quantization = true;
    config.lut_file = "quant_one_lut.csv";
    config.map_file = "quant_one_map.csv";

    BlockMaxQuantizedIndex index(config);

    // Load documents
    std::ifstream doc_file("/home/ubuntu/msmarco/full_data/full_docs.txt");
    if (!doc_file.is_open()) {
        std::cerr << "Error: Could not open documents file" << std::endl;
        return 1;
    }

    std::vector<SparseDoc> documents;
    std::string line;
    while (std::getline(doc_file, line)) {
        SparseDoc doc;
        std::istringstream iss(line);
        std::string token;

        while (iss >> token) {
            size_t colon_pos = token.find(':');
            if (colon_pos != std::string::npos) {
                term_t term = std::stoi(token.substr(0, colon_pos));
                float score = std::stof(token.substr(colon_pos + 1));
                doc.emplace_back(term, score);
            }
        }

        if (!doc.empty()) {
            documents.push_back(std::move(doc));
        }
    }

    std::cout << "Building index with " << documents.size() << " documents..." << std::endl;
    index.build(documents);
    documents.clear();  // Free memory

    // Load queries
    std::cout << "Loading queries..." << std::endl;
    auto queries = loadQueries(index_path, num_queries);
    std::cout << "Loaded " << queries.size() << " queries" << std::endl;

    // Warm-up
    std::cout << "Warming up..." << std::endl;
    BlockMaxQuantizedIndex::QueryParams params;
    params.alpha = alpha;
    params.top_k_prime = 500;

    for (size_t i = 0; i < std::min(size_t(10), queries.size()); ++i) {
        QueryStats stats;
        index.query(queries[i], params, &stats);
    }

    // Profile queries
    std::cout << "\nProfiling " << queries.size() << " queries..." << std::endl;
    ProfileHelper::getInstance().reset();

    for (size_t i = 0; i < queries.size(); ++i) {
        {
            PROFILE_SCOPE("query_total");

            QueryStats stats;
            auto result = index.query(queries[i], params, &stats);

            // Record stats from QueryStats
            auto& prof = ProfileHelper::getInstance();
            auto start_cycles = ProfileHelper::rdtsc();
            // Simulate recording (actual implementation would need access to internals)
            auto end_cycles = ProfileHelper::rdtsc();
        }

        if ((i + 1) % 10 == 0) {
            std::cout << "  Processed " << (i + 1) << " queries" << std::endl;
        }
    }

    // Print report
    ProfileHelper::getInstance().printReport(cpu_freq_ghz);

    // Additional statistics
    std::cout << "\n=== Additional Analysis ===" << std::endl;
    std::cout << "Number of queries: " << queries.size() << std::endl;

    const auto& stats = ProfileHelper::getInstance().getStats();
    if (stats.find("query_total") != stats.end()) {
        const auto& query_stat = stats.at("query_total");
        double avg_cycles = query_stat.avg_cycles();
        double avg_time_ms = (avg_cycles / cpu_freq_ghz) / 1e6;
        double qps = 1000.0 / avg_time_ms;

        std::cout << "Average query time: " << avg_time_ms << " ms" << std::endl;
        std::cout << "QPS: " << qps << std::endl;
        std::cout << "Average cycles per query: " << avg_cycles << std::endl;
    }

    return 0;
}
