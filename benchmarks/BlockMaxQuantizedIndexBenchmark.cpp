// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Block-Max Quantized Index Benchmark (QBlock Algorithm Implementation)
 *
 * This benchmark implements the same algorithm as QBlock BitQ:
 * 1. Build quantized inverted index with blocks
 * 2. Query with block selection, scatter-add, and reranking
 * 3. Measure build time, query time, memory, and recall
 *
 * Dataset: MSMarco v1 SPLADE
 */

#include "diagon/index/BlockMaxQuantizedIndex.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using namespace diagon::index;

// ==================== CSR Reader ====================

using term_t = uint32_t;
using indptr_t = uint64_t;
using metadata_t = uint64_t;

struct CsrMatrix {
    metadata_t n_row;
    metadata_t n_col;
    metadata_t n_value;
    std::vector<indptr_t> indptr;
    std::vector<uint16_t> indices;  // QBlock uses uint16_t
    std::vector<float> values;
};

CsrMatrix loadCsrMatrix(const std::string& file_path) {
    std::cout << "Loading: " << file_path << std::endl;

    CsrMatrix result;

    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "Error: Unable to open file " << file_path << std::endl;
        return result;
    }

    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        close(fd);
        std::cerr << "Error: Unable to stat file" << std::endl;
        return result;
    }

    size_t file_size = sb.st_size;
    void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        std::cerr << "Error: mmap failed" << std::endl;
        return result;
    }

    madvise(mapped, file_size, MADV_SEQUENTIAL);

    const char* data = static_cast<const char*>(mapped);
    size_t offset = 0;

    // Read header
    result.n_row = *reinterpret_cast<const metadata_t*>(data + offset);
    offset += sizeof(metadata_t);
    result.n_col = *reinterpret_cast<const metadata_t*>(data + offset);
    offset += sizeof(metadata_t);
    result.n_value = *reinterpret_cast<const metadata_t*>(data + offset);
    offset += sizeof(metadata_t);

    std::cout << "  Rows: " << result.n_row << ", Cols: " << result.n_col
              << ", Values: " << result.n_value << std::endl;

    // Read indptr
    const indptr_t* indptr_ptr = reinterpret_cast<const indptr_t*>(data + offset);
    result.indptr.assign(indptr_ptr, indptr_ptr + result.n_row + 1);
    offset += (result.n_row + 1) * sizeof(indptr_t);

    // Read indices (uint32 in file, convert to uint16)
    const uint32_t* indices_ptr = reinterpret_cast<const uint32_t*>(data + offset);
    result.indices.resize(result.n_value);
    for (size_t i = 0; i < result.n_value; ++i) {
        result.indices[i] = static_cast<uint16_t>(indices_ptr[i]);
    }
    offset += result.n_value * sizeof(uint32_t);

    // Read values
    const float* values_ptr = reinterpret_cast<const float*>(data + offset);
    result.values.assign(values_ptr, values_ptr + result.n_value);

    munmap(mapped, file_size);
    close(fd);

    std::cout << "  Loaded successfully" << std::endl;
    return result;
}

std::vector<SparseDoc> convertToSparseDocs(const CsrMatrix& matrix, size_t max_docs = 0) {
    size_t num_docs = max_docs > 0 ? std::min(max_docs, (size_t)matrix.n_row) : matrix.n_row;

    std::vector<SparseDoc> docs(num_docs);

    for (size_t i = 0; i < num_docs; ++i) {
        indptr_t start = matrix.indptr[i];
        indptr_t end = matrix.indptr[i + 1];
        size_t nnz = end - start;

        docs[i].reserve(nnz);
        for (size_t j = start; j < end; ++j) {
            docs[i].emplace_back(matrix.indices[j], matrix.values[j]);
        }
    }

    return docs;
}

// ==================== Ground Truth ====================

std::vector<std::vector<uint32_t>> loadGroundTruth(const std::string& file_path) {
    std::cout << "Loading ground truth: " << file_path << std::endl;

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open ground truth file" << std::endl;
        return {};
    }

    std::vector<std::vector<uint32_t>> ground_truth;
    std::string line;

    while (std::getline(file, line)) {
        std::vector<uint32_t> row;
        size_t pos = 0;

        while (pos < line.size()) {
            size_t comma = line.find(',', pos);
            if (comma == std::string::npos)
                comma = line.size();

            std::string token = line.substr(pos, comma - pos);
            if (!token.empty()) {
                row.push_back(std::stoul(token));
            }

            pos = comma + 1;
        }

        ground_truth.push_back(row);
    }

    std::cout << "  Loaded " << ground_truth.size() << " queries" << std::endl;
    return ground_truth;
}

// ==================== Benchmark ====================

struct BenchmarkConfig {
    size_t max_docs = 0;  // 0 = all
    size_t max_queries = 100;
    size_t top_k = 10;
    size_t top_k_prime = 500;  // QBlock uses 500 for 12-bin configuration
    std::vector<float> alphas = {0.3f, 0.5f, 0.7f, 1.0f};
    bool alpha_mass = true;

    // Custom quantization
    bool use_custom_quantization = false;
    std::string lut_file;
    std::string map_file;
};

struct BenchmarkResults {
    double build_time_ms = 0.0;
    size_t index_memory_bytes = 0;

    struct QueryResult {
        float alpha = 0.0f;
        double avg_query_time_ms = 0.0;
        double qps = 0.0;
        double avg_blocks_selected = 0.0;
        double avg_score_ops = 0.0;
        double recall_at_k = 0.0;
        // Phase timing breakdowns (matching QBlock's metrics)
        double avg_block_selection_ms = 0.0;
        double avg_scatter_add_ms = 0.0;
        double avg_scatter_add_part1_ms = 0.0;  // Score accumulation
        double avg_scatter_add_part2_ms = 0.0;  // TopK processing
        double avg_reranking_ms = 0.0;
    };

    std::vector<QueryResult> query_results;
};

double calculateRecall(const std::vector<doc_id_t>& results,
                       const std::vector<uint32_t>& ground_truth, size_t k) {
    std::unordered_set<uint32_t> gt_set(ground_truth.begin(), ground_truth.end());
    size_t hits = 0;

    for (size_t i = 0; i < std::min(k, results.size()); ++i) {
        if (gt_set.count(results[i]) > 0) {
            hits++;
        }
    }

    return static_cast<double>(hits) / std::min(k, ground_truth.size());
}

// Forward declaration
void testDocumentRetrieval(const BlockMaxQuantizedIndex& index,
                           const std::vector<SparseDoc>& queries, const BenchmarkConfig& config);

BenchmarkResults runBenchmark(const BenchmarkConfig& config) {
    BenchmarkResults results;

    // Load dataset
    std::string docs_path =
        "/home/ubuntu/bitq-code/cpp-sparse-ann/Datasets/msmarco_v1_splade/docs.csr";
    std::string queries_path =
        "/home/ubuntu/bitq-code/cpp-sparse-ann/Datasets/msmarco_v1_splade/queries.csr";
    std::string truth_path = "/home/ubuntu/bitq-code/cpp-sparse-ann/Datasets/msmarco_v1_splade/"
                             "cocondense_ground_truth_int.txt";

    auto docs_matrix = loadCsrMatrix(docs_path);
    auto queries_matrix = loadCsrMatrix(queries_path);
    auto ground_truth = loadGroundTruth(truth_path);

    // Convert to sparse docs
    std::cout << "\nConverting to sparse documents..." << std::endl;
    auto docs = convertToSparseDocs(docs_matrix, config.max_docs);
    auto queries = convertToSparseDocs(queries_matrix);

    std::cout << "  Documents: " << docs.size() << std::endl;
    std::cout << "  Queries: " << queries.size() << std::endl;

    // Build index
    std::cout << "\nBuilding Block-Max Quantized Index..." << std::endl;

    BlockMaxQuantizedIndex::Config index_config;
    index_config.num_quantization_bins = 256;
    index_config.window_size = 500000;  // 0.5M window size (optimal for normal CPU)
    index_config.max_score = 3.0f;

    // Custom quantization (if enabled)
    if (config.use_custom_quantization) {
        index_config.use_custom_quantization = true;
        index_config.lut_file = config.lut_file;
        index_config.map_file = config.map_file;
        std::cout << "Using custom quantization:" << std::endl;
        std::cout << "  LUT file: " << config.lut_file << std::endl;
        std::cout << "  Map file: " << config.map_file << std::endl;
    }

    BlockMaxQuantizedIndex index(index_config);

    auto build_start = std::chrono::high_resolution_clock::now();
    index.build(docs);
    auto build_end = std::chrono::high_resolution_clock::now();

    results.build_time_ms =
        std::chrono::duration<double, std::milli>(build_end - build_start).count();
    results.index_memory_bytes = index.memoryUsageBytes();

    std::cout << "  Build time: " << results.build_time_ms << " ms" << std::endl;
    std::cout << "  Throughput: " << (docs.size() / (results.build_time_ms / 1000.0)) << " docs/sec"
              << std::endl;
    std::cout << "  Memory usage: " << (results.index_memory_bytes / (1024.0 * 1024.0)) << " MB"
              << std::endl;
    std::cout << "  Windows: " << index.numWindows() << std::endl;

    // Query with different alpha values
    size_t num_queries = std::min(config.max_queries, queries.size());

    for (float alpha : config.alphas) {
        std::cout << "\nQuerying with alpha = " << alpha << "..." << std::endl;

        BlockMaxQuantizedIndex::QueryParams query_params;
        query_params.top_k = config.top_k;
        query_params.top_k_prime = config.top_k_prime;
        query_params.alpha = alpha;
        query_params.alpha_mass = config.alpha_mass;

        BenchmarkResults::QueryResult qr;
        qr.alpha = alpha;

        double total_query_time = 0.0;
        double total_blocks_selected = 0.0;
        double total_score_ops = 0.0;
        double total_recall = 0.0;
        // Phase timing
        double total_block_selection = 0.0;
        double total_scatter_add = 0.0;
        double total_scatter_add_part1 = 0.0;
        double total_scatter_add_part2 = 0.0;
        double total_reranking = 0.0;

        for (size_t i = 0; i < num_queries; ++i) {
            QueryStats stats;
            auto result = index.query(queries[i], query_params, &stats);

            total_query_time += stats.total_ms;
            total_blocks_selected += stats.selected_blocks;
            total_score_ops += stats.score_operations;
            // Phase timing
            total_block_selection += stats.block_selection_ms;
            total_scatter_add += stats.scatter_add_ms;
            total_scatter_add_part1 += stats.scatter_add_part1_ms;
            total_scatter_add_part2 += stats.scatter_add_part2_ms;
            total_reranking += stats.reranking_ms;

            if (i < ground_truth.size()) {
                double recall = calculateRecall(result, ground_truth[i], config.top_k);
                total_recall += recall;
            }

            if ((i + 1) % 10 == 0) {
                std::cout << "  Processed " << (i + 1) << " queries" << std::endl;
            }
        }

        qr.avg_query_time_ms = total_query_time / num_queries;
        qr.qps = 1000.0 / qr.avg_query_time_ms;
        qr.avg_blocks_selected = total_blocks_selected / num_queries;
        qr.avg_score_ops = total_score_ops / num_queries;
        qr.recall_at_k = total_recall / num_queries;
        // Phase timing averages
        qr.avg_block_selection_ms = total_block_selection / num_queries;
        qr.avg_scatter_add_ms = total_scatter_add / num_queries;
        qr.avg_scatter_add_part1_ms = total_scatter_add_part1 / num_queries;
        qr.avg_scatter_add_part2_ms = total_scatter_add_part2 / num_queries;
        qr.avg_reranking_ms = total_reranking / num_queries;

        results.query_results.push_back(qr);

        std::cout << "  Avg query time: " << qr.avg_query_time_ms << " ms" << std::endl;
        std::cout << "  QPS: " << qr.qps << std::endl;
        std::cout << "  Avg blocks selected: " << qr.avg_blocks_selected << std::endl;
        std::cout << "  Avg score ops: " << qr.avg_score_ops << std::endl;
        std::cout << "  Recall@" << config.top_k << ": " << (qr.recall_at_k * 100.0) << "%"
                  << std::endl;
        std::cout << "\n  Timing Breakdown:" << std::endl;
        std::cout << "    Block selection:   " << qr.avg_block_selection_ms << " ms" << std::endl;
        std::cout << "    Scatter-add total: " << qr.avg_scatter_add_ms << " ms" << std::endl;
        std::cout << "      Part 1 (accum):  " << qr.avg_scatter_add_part1_ms << " ms" << std::endl;
        std::cout << "      Part 2 (TopK):   " << qr.avg_scatter_add_part2_ms << " ms" << std::endl;
        std::cout << "    Reranking:         " << qr.avg_reranking_ms << " ms" << std::endl;
    }

    // Test document retrieval
    testDocumentRetrieval(index, queries, config);

    return results;
}

void testDocumentRetrieval(const BlockMaxQuantizedIndex& index,
                           const std::vector<SparseDoc>& queries, const BenchmarkConfig& config) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing Direct Document Retrieval" << std::endl;
    std::cout << "========================================" << std::endl;

    // Query to get some document IDs
    BlockMaxQuantizedIndex::QueryParams query_params;
    query_params.top_k = 5;
    query_params.alpha = 0.5f;

    std::cout << "Running sample query to get document IDs..." << std::endl;
    auto result_ids = const_cast<BlockMaxQuantizedIndex&>(index).query(queries[0], query_params);

    std::cout << "  Found " << result_ids.size() << " results" << std::endl;

    // Test single document retrieval
    if (!result_ids.empty()) {
        std::cout << "\nTesting single document retrieval:" << std::endl;
        doc_id_t first_doc_id = result_ids[0];

        auto start = std::chrono::high_resolution_clock::now();
        const auto& doc = index.getDocument(first_doc_id);
        auto end = std::chrono::high_resolution_clock::now();

        double retrieval_time = std::chrono::duration<double, std::micro>(end - start).count();

        std::cout << "  Doc ID: " << first_doc_id << std::endl;
        std::cout << "  Num terms: " << doc.size() << std::endl;
        std::cout << "  Retrieval time: " << retrieval_time << " µs" << std::endl;

        // Show first few terms
        std::cout << "  First 5 terms: ";
        for (size_t i = 0; i < std::min(size_t(5), doc.size()); ++i) {
            std::cout << "(" << doc[i].term << "," << doc[i].score << ") ";
        }
        std::cout << std::endl;
    }

    // Test batch document retrieval
    if (result_ids.size() >= 3) {
        std::cout << "\nTesting batch document retrieval:" << std::endl;
        std::vector<doc_id_t> batch_ids(result_ids.begin(), result_ids.begin() + 3);

        auto start = std::chrono::high_resolution_clock::now();
        auto batch_docs = index.getDocuments(batch_ids);
        auto end = std::chrono::high_resolution_clock::now();

        double batch_time = std::chrono::duration<double, std::micro>(end - start).count();

        std::cout << "  Batch size: " << batch_ids.size() << std::endl;
        std::cout << "  Retrieved: " << batch_docs.size() << " documents" << std::endl;
        std::cout << "  Batch retrieval time: " << batch_time << " µs" << std::endl;
        std::cout << "  Avg per doc: " << (batch_time / batch_docs.size()) << " µs" << std::endl;

        size_t total_terms = 0;
        for (const auto& doc : batch_docs) {
            total_terms += doc.size();
        }
        std::cout << "  Total terms retrieved: " << total_terms << std::endl;
    }

    // Test error handling
    std::cout << "\nTesting error handling:" << std::endl;
    try {
        doc_id_t invalid_id = index.numDocuments() + 1000;
        std::cout << "  Attempting to retrieve invalid doc ID " << invalid_id << "..." << std::endl;
        [[maybe_unused]] const auto& invalid_doc = index.getDocument(invalid_id);
        std::cout << "  ERROR: Should have thrown exception!" << std::endl;
    } catch (const std::out_of_range& e) {
        std::cout << "  ✓ Correctly threw exception: " << e.what() << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
}

void printResults(const BenchmarkResults& results, const BenchmarkConfig& config) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Block-Max Quantized Index Benchmark Results" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\n--- Build Performance ---" << std::endl;
    std::cout << "Build time: " << results.build_time_ms << " ms" << std::endl;
    std::cout << "Memory usage: " << (results.index_memory_bytes / (1024.0 * 1024.0)) << " MB"
              << std::endl;

    std::cout << "\n--- Query Performance ---" << std::endl;
    std::cout << "Alpha | QPS    | Latency(ms) | Recall@" << config.top_k << " | Blocks | Score Ops"
              << std::endl;
    std::cout << "------|--------|-------------|---------|--------|----------" << std::endl;

    for (const auto& qr : results.query_results) {
        printf("%.1f   | %6.2f | %11.2f | %6.2f%% | %6.0f | %10.0f\n", qr.alpha, qr.qps,
               qr.avg_query_time_ms, qr.recall_at_k * 100.0, qr.avg_blocks_selected,
               qr.avg_score_ops);
    }

    std::cout << "\n========================================" << std::endl;
}

int main(int argc, char** argv) {
    BenchmarkConfig config;

    // Parse arguments
    bool custom_alphas = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--max-docs" && i + 1 < argc) {
            config.max_docs = std::stoull(argv[++i]);
        } else if (arg == "--max-queries" && i + 1 < argc) {
            config.max_queries = std::stoull(argv[++i]);
        } else if (arg == "--top-k" && i + 1 < argc) {
            config.top_k = std::stoull(argv[++i]);
        } else if (arg == "--top-k-prime" && i + 1 < argc) {
            config.top_k_prime = std::stoull(argv[++i]);
        } else if (arg == "--lut-file" && i + 1 < argc) {
            config.lut_file = argv[++i];
            config.use_custom_quantization = true;
        } else if (arg == "--map-file" && i + 1 < argc) {
            config.map_file = argv[++i];
            config.use_custom_quantization = true;
        } else if (arg == "--alpha") {
            // Parse all following alpha values until next argument or end
            if (!custom_alphas) {
                config.alphas.clear();
                custom_alphas = true;
            }
            ++i;
            while (i < argc && argv[i][0] != '-') {
                config.alphas.push_back(std::stof(argv[i]));
                ++i;
            }
            --i;  // Back up one since the outer loop will increment
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Block-Max Quantized Index Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Max docs: " << (config.max_docs == 0 ? "all" : std::to_string(config.max_docs))
              << std::endl;
    std::cout << "Max queries: " << config.max_queries << std::endl;
    std::cout << "Top-k: " << config.top_k << std::endl;
    std::cout << "Top-k': " << config.top_k_prime << std::endl;
    std::cout << "Alpha values: ";
    for (float alpha : config.alphas) {
        std::cout << alpha << " ";
    }
    std::cout << std::endl;

    // Debug: show custom quantization config
    if (config.use_custom_quantization) {
        std::cout << "Custom quantization: ENABLED" << std::endl;
        std::cout << "  LUT file: " << config.lut_file << std::endl;
        std::cout << "  Map file: " << config.map_file << std::endl;
    } else {
        std::cout << "Custom quantization: DISABLED" << std::endl;
    }

    std::cout << "========================================\n" << std::endl;

    auto results = runBenchmark(config);
    printResults(results, config);

    return 0;
}
