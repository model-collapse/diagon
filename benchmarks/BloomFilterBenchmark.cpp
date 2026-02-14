// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Bloom Filter Benchmark on MSMarco v1 SPLADE Dataset
 *
 * Measures:
 * 1. Build time: Creating bloom filters for document collection
 * 2. Query time: Evaluating membership queries
 * 3. False positive rate: Empirical FPR measurement
 * 4. Memory usage: Space overhead per document
 *
 * Dataset: msmarco_v1_splade
 * - Docs: ~8.8M documents in CSR format
 * - Queries: ~6,980 queries in CSR format
 * - Vocabulary: ~30K terms
 */

#include "diagon/util/BloomFilter.h"
#include "diagon/util/CityHash.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using namespace diagon::util;

// ==================== Data Types (from QBlock) ====================

using term_t = uint16_t;      // QBlock uses uint16_t for term indices
using indptr_t = uint64_t;    // QBlock uses uint64_t for indptr
using metadata_t = uint64_t;  // QBlock uses uint64_t for metadata

struct SparseVectorElement {
    term_t index;
    float value;

    SparseVectorElement()
        : index(0)
        , value(0.0f) {}
    SparseVectorElement(term_t idx, float val)
        : index(idx)
        , value(val) {}
};

using SparseVector = std::vector<SparseVectorElement>;

struct CsrMetaData {
    metadata_t n_col;
    metadata_t n_row;
    metadata_t n_value;

    CsrMetaData()
        : n_col(0)
        , n_row(0)
        , n_value(0) {}
};

struct CsrMatrix {
    CsrMetaData metadata;
    std::vector<indptr_t> indptr;  // uint64_t
    std::vector<term_t> indices;   // uint16_t (will read uint32_t from file and convert)
    std::vector<float> values;

    SparseVector getVector(size_t i) const {
        if (i >= indptr.size() - 1) {
            return SparseVector();
        }

        indptr_t start = indptr[i];
        indptr_t end = indptr[i + 1];
        size_t len = end - start;

        SparseVector result;
        result.reserve(len);

        for (size_t j = 0; j < len; ++j) {
            result.emplace_back(indices[start + j], values[start + j]);
        }

        return result;
    }
};

// ==================== CSR Reader ====================

CsrMatrix loadCsrMatrix(const std::string& file_path) {
    std::cout << "Loading CSR matrix: " << file_path << std::endl;

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
    int mmap_flags = MAP_PRIVATE;
#ifdef MAP_POPULATE
    mmap_flags |= MAP_POPULATE;
#endif
    void* mapped = mmap(nullptr, file_size, PROT_READ, mmap_flags, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        std::cerr << "Error: mmap failed" << std::endl;
        return result;
    }

    madvise(mapped, file_size, MADV_SEQUENTIAL);

    const char* data = static_cast<const char*>(mapped);
    size_t offset = 0;

    // Read header (3 x uint64_t values)
    result.metadata.n_row = *reinterpret_cast<const metadata_t*>(data + offset);
    offset += sizeof(metadata_t);
    result.metadata.n_col = *reinterpret_cast<const metadata_t*>(data + offset);
    offset += sizeof(metadata_t);
    result.metadata.n_value = *reinterpret_cast<const metadata_t*>(data + offset);
    offset += sizeof(metadata_t);

    std::cout << "  Rows: " << result.metadata.n_row << " (0x" << std::hex << result.metadata.n_row
              << std::dec << ")" << std::endl;
    std::cout << "  Cols: " << result.metadata.n_col << " (0x" << std::hex << result.metadata.n_col
              << std::dec << ")" << std::endl;
    std::cout << "  Values: " << result.metadata.n_value << " (0x" << std::hex
              << result.metadata.n_value << std::dec << ")" << std::endl;
    std::cout << "  Offset after header: " << offset << " bytes" << std::endl;

    // Read indptr
    const indptr_t* indptr_ptr = reinterpret_cast<const indptr_t*>(data + offset);
    result.indptr.assign(indptr_ptr, indptr_ptr + result.metadata.n_row + 1);
    offset += (result.metadata.n_row + 1) * sizeof(indptr_t);

    // Read indices (stored as uint32_t in file, convert to term_t)
    const uint32_t* indices_ptr = reinterpret_cast<const uint32_t*>(data + offset);
    result.indices.resize(result.metadata.n_value);
    for (size_t i = 0; i < result.metadata.n_value; ++i) {
        result.indices[i] = static_cast<term_t>(indices_ptr[i]);
    }
    offset += result.metadata.n_value * sizeof(uint32_t);

    // Read values
    const float* values_ptr = reinterpret_cast<const float*>(data + offset);
    result.values.assign(values_ptr, values_ptr + result.metadata.n_value);

    munmap(mapped, file_size);
    close(fd);

    std::cout << "  Loaded successfully" << std::endl;
    return result;
}

// ==================== Benchmark Configuration ====================

struct BenchmarkConfig {
    // Bloom filter parameters
    size_t bits_per_element = 10;  // ~1% FPR with 7 hash functions
    size_t num_hash_functions = 7;

    // Dataset parameters
    size_t max_docs = 0;       // 0 = all documents
    size_t max_queries = 100;  // Test queries

    // Sampling for FPR measurement
    size_t fpr_sample_docs = 10000;  // Sample size for FPR measurement
    size_t fpr_test_terms = 1000;    // Test terms not in filter
};

// ==================== Bloom Filter Index ====================

class BloomFilterIndex {
public:
    BloomFilterIndex(size_t bits_per_elem, size_t num_hashes)
        : bits_per_elem_(bits_per_elem)
        , num_hashes_(num_hashes) {}

    void build(const CsrMatrix& docs, size_t max_docs = 0) {
        auto start = std::chrono::high_resolution_clock::now();

        size_t num_docs = max_docs > 0 ? std::min(max_docs, (size_t)docs.metadata.n_row)
                                       : docs.metadata.n_row;

        filters_.resize(num_docs);
        doc_sizes_.resize(num_docs);

        std::cout << "\nBuilding bloom filters for " << num_docs << " documents..." << std::endl;

        for (size_t i = 0; i < num_docs; ++i) {
            auto vec = docs.getVector(i);
            size_t nnz = vec.size();

            doc_sizes_[i] = nnz;

            // Calculate filter size
            size_t filter_bits = bits_per_elem_ * nnz;
            size_t filter_bytes = (filter_bits + 7) / 8;

            // Create bloom filter
            filters_[i] = std::make_shared<BloomFilter>(filter_bytes, num_hashes_, i);

            // Add terms
            for (const auto& elem : vec) {
                // Hash term index directly
                filters_[i]->addHash(elem.index);
            }

            if ((i + 1) % 100000 == 0) {
                std::cout << "  Processed " << (i + 1) << " documents" << std::endl;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        build_time_ms_ = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "Build complete in " << build_time_ms_ << " ms" << std::endl;
        std::cout << "  Throughput: " << (num_docs / (build_time_ms_ / 1000.0)) << " docs/sec"
                  << std::endl;
    }

    bool mightContain(size_t doc_id, term_t term) const {
        if (doc_id >= filters_.size())
            return false;
        return filters_[doc_id]->containsHash(term);
    }

    size_t memoryUsageBytes() const {
        size_t total = 0;
        for (const auto& filter : filters_) {
            if (filter) {
                total += filter->memoryUsageBytes();
            }
        }
        return total;
    }

    double avgBitsPerDocument() const {
        if (filters_.empty())
            return 0.0;
        return (memoryUsageBytes() * 8.0) / filters_.size();
    }

    double buildTimeMs() const { return build_time_ms_; }
    size_t numDocuments() const { return filters_.size(); }

    const std::vector<size_t>& docSizes() const { return doc_sizes_; }

private:
    size_t bits_per_elem_;
    size_t num_hashes_;
    std::vector<std::shared_ptr<BloomFilter>> filters_;
    std::vector<size_t> doc_sizes_;
    double build_time_ms_ = 0.0;
};

// ==================== Query Benchmark ====================

struct QueryStats {
    double total_time_ms = 0.0;
    size_t total_checks = 0;
    size_t total_positives = 0;
    double avg_time_per_query_ms = 0.0;
    double throughput_checks_per_sec = 0.0;
};

QueryStats runQueryBenchmark(const BloomFilterIndex& index, const CsrMatrix& queries,
                             const CsrMatrix& docs, size_t max_queries) {
    std::cout << "\nRunning query benchmark..." << std::endl;

    QueryStats stats;
    size_t num_queries = std::min(max_queries, (size_t)queries.metadata.n_row);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t q = 0; q < num_queries; ++q) {
        auto query = queries.getVector(q);

        // Check all documents for query terms
        for (size_t d = 0; d < index.numDocuments(); ++d) {
            for (const auto& elem : query) {
                bool contains = index.mightContain(d, elem.index);
                stats.total_checks++;
                if (contains) {
                    stats.total_positives++;
                }
            }
        }

        if ((q + 1) % 10 == 0) {
            std::cout << "  Processed " << (q + 1) << " queries" << std::endl;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    stats.total_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    stats.avg_time_per_query_ms = stats.total_time_ms / num_queries;
    stats.throughput_checks_per_sec = (stats.total_checks / (stats.total_time_ms / 1000.0));

    return stats;
}

// ==================== False Positive Rate Measurement ====================

struct FprStats {
    size_t true_positives = 0;
    size_t false_positives = 0;
    size_t true_negatives = 0;
    size_t false_negatives = 0;
    double fpr = 0.0;
    double recall = 0.0;
};

FprStats measureFalsePositiveRate(const BloomFilterIndex& index, const CsrMatrix& docs,
                                  size_t sample_docs, size_t test_terms) {
    std::cout << "\nMeasuring false positive rate..." << std::endl;

    FprStats stats;

    // Sample documents
    size_t num_sample = std::min(sample_docs, index.numDocuments());
    std::cout << "  Testing " << num_sample << " documents" << std::endl;

    for (size_t i = 0; i < num_sample; ++i) {
        auto doc = docs.getVector(i);

        // Build ground truth set
        std::vector<bool> ground_truth(docs.metadata.n_col, false);
        for (const auto& elem : doc) {
            ground_truth[elem.index] = true;
        }

        // Test positive terms (should be in filter)
        for (const auto& elem : doc) {
            bool in_filter = index.mightContain(i, elem.index);
            if (in_filter) {
                stats.true_positives++;
            } else {
                stats.false_negatives++;
            }
        }

        // Test random negative terms (should not be in filter)
        size_t negatives_tested = 0;
        for (term_t term = 0; term < docs.metadata.n_col && negatives_tested < test_terms; ++term) {
            if (!ground_truth[term]) {
                bool in_filter = index.mightContain(i, term);
                if (in_filter) {
                    stats.false_positives++;
                } else {
                    stats.true_negatives++;
                }
                negatives_tested++;
            }
        }
    }

    // Calculate metrics
    stats.fpr = (double)stats.false_positives / (stats.false_positives + stats.true_negatives);
    stats.recall = (double)stats.true_positives / (stats.true_positives + stats.false_negatives);

    return stats;
}

// ==================== Main Benchmark ====================

void printConfig(const BenchmarkConfig& config) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Bloom Filter Benchmark Configuration" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Bits per element: " << config.bits_per_element << std::endl;
    std::cout << "Hash functions: " << config.num_hash_functions << std::endl;
    std::cout << "Max documents: "
              << (config.max_docs == 0 ? "all" : std::to_string(config.max_docs)) << std::endl;
    std::cout << "Max queries: " << config.max_queries << std::endl;
    std::cout << "FPR sample docs: " << config.fpr_sample_docs << std::endl;
    std::cout << "FPR test terms: " << config.fpr_test_terms << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void printResults(const BloomFilterIndex& index, const QueryStats& query_stats,
                  const FprStats& fpr_stats) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Benchmark Results" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\n--- Build Statistics ---" << std::endl;
    std::cout << "Number of documents: " << index.numDocuments() << std::endl;
    std::cout << "Build time: " << index.buildTimeMs() << " ms" << std::endl;
    std::cout << "Throughput: " << (index.numDocuments() / (index.buildTimeMs() / 1000.0))
              << " docs/sec" << std::endl;

    std::cout << "\n--- Memory Statistics ---" << std::endl;
    std::cout << "Total memory: " << (index.memoryUsageBytes() / (1024.0 * 1024.0)) << " MB"
              << std::endl;
    std::cout << "Avg bits per doc: " << index.avgBitsPerDocument() << " bits" << std::endl;
    std::cout << "Avg bytes per doc: " << (index.avgBitsPerDocument() / 8.0) << " bytes"
              << std::endl;

    std::cout << "\n--- Query Statistics ---" << std::endl;
    std::cout << "Total time: " << query_stats.total_time_ms << " ms" << std::endl;
    std::cout << "Avg time per query: " << query_stats.avg_time_per_query_ms << " ms" << std::endl;
    std::cout << "Total checks: " << query_stats.total_checks << std::endl;
    std::cout << "Throughput: " << (query_stats.throughput_checks_per_sec / 1e6) << " M checks/sec"
              << std::endl;

    std::cout << "\n--- False Positive Rate ---" << std::endl;
    std::cout << "True positives: " << fpr_stats.true_positives << std::endl;
    std::cout << "False positives: " << fpr_stats.false_positives << std::endl;
    std::cout << "True negatives: " << fpr_stats.true_negatives << std::endl;
    std::cout << "False negatives: " << fpr_stats.false_negatives << std::endl;
    std::cout << "FPR: " << (fpr_stats.fpr * 100.0) << "%" << std::endl;
    std::cout << "Recall: " << (fpr_stats.recall * 100.0) << "%" << std::endl;

    std::cout << "\n========================================\n" << std::endl;
}

int main(int argc, char** argv) {
    BenchmarkConfig config;

    // Parse command line arguments
    std::string docs_path =
        "/home/ubuntu/bitq-code/cpp-sparse-ann/Datasets/msmarco_v1_splade/docs.csr";
    std::string queries_path =
        "/home/ubuntu/bitq-code/cpp-sparse-ann/Datasets/msmarco_v1_splade/queries.csr";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--docs" && i + 1 < argc) {
            docs_path = argv[++i];
        } else if (arg == "--queries" && i + 1 < argc) {
            queries_path = argv[++i];
        } else if (arg == "--bits-per-elem" && i + 1 < argc) {
            config.bits_per_element = std::stoull(argv[++i]);
        } else if (arg == "--num-hashes" && i + 1 < argc) {
            config.num_hash_functions = std::stoull(argv[++i]);
        } else if (arg == "--max-docs" && i + 1 < argc) {
            config.max_docs = std::stoull(argv[++i]);
        } else if (arg == "--max-queries" && i + 1 < argc) {
            config.max_queries = std::stoull(argv[++i]);
        } else if (arg == "--fpr-sample" && i + 1 < argc) {
            config.fpr_sample_docs = std::stoull(argv[++i]);
        }
    }

    printConfig(config);

    // Load datasets
    std::cout << "========================================" << std::endl;
    std::cout << "Loading Datasets" << std::endl;
    std::cout << "========================================\n" << std::endl;

    auto docs = loadCsrMatrix(docs_path);
    if (docs.metadata.n_row == 0) {
        std::cerr << "Failed to load documents" << std::endl;
        return 1;
    }

    auto queries = loadCsrMatrix(queries_path);
    if (queries.metadata.n_row == 0) {
        std::cerr << "Failed to load queries" << std::endl;
        return 1;
    }

    // Build bloom filter index
    BloomFilterIndex index(config.bits_per_element, config.num_hash_functions);
    index.build(docs, config.max_docs);

    // Run query benchmark
    auto query_stats = runQueryBenchmark(index, queries, docs, config.max_queries);

    // Measure false positive rate
    auto fpr_stats = measureFalsePositiveRate(index, docs, config.fpr_sample_docs,
                                              config.fpr_test_terms);

    // Print results
    printResults(index, query_stats, fpr_stats);

    return 0;
}
