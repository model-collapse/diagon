// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <vector>
#include <memory>
#include <cstdint>

namespace diagon {
namespace index {

/**
 * Block-Max Quantized Inverted Index (similar to QBlock BitQ)
 *
 * Algorithm:
 * 1. Index: Documents organized by [term][quantized_block][window]
 * 2. Query: Block selection + pruning + scatter-add + reranking
 *
 * Key concepts:
 * - Quantization: Float scores → 256 bins (uint8)
 * - Windows: Documents chunked into 64K windows for memory locality
 * - Block-max pruning: Select top blocks by contribution (alpha parameter)
 */

using term_t = uint32_t;
using doc_id_t = uint32_t;
using score_t = float;

struct SparseElement {
    term_t term;
    score_t score;

    SparseElement() : term(0), score(0.0f) {}
    SparseElement(term_t t, score_t s) : term(t), score(s) {}
};

using SparseDoc = std::vector<SparseElement>;

struct QueryStats {
    size_t total_blocks = 0;
    size_t selected_blocks = 0;
    size_t score_operations = 0;
    double block_selection_ms = 0.0;
    double scatter_add_ms = 0.0;
    double reranking_ms = 0.0;
    double total_ms = 0.0;
};

class BlockMaxQuantizedIndex {
public:
    /**
     * Configuration parameters
     */
    struct Config {
        size_t num_quantization_bins = 256;  // Number of quantization bins
        size_t window_size = 65536;           // Documents per window
        float max_score = 3.0f;               // Maximum score for quantization

        Config() = default;
    };

    /**
     * Query parameters
     */
    struct QueryParams {
        size_t top_k = 10;           // Number of results to return
        size_t top_k_prime = 50;     // Candidates for reranking
        float alpha = 0.5f;          // Block selection parameter (0.0-1.0)
        bool alpha_mass = true;      // Use alpha-mass (true) or max-ratio (false)

        QueryParams() = default;
    };

    /**
     * Constructor
     */
    explicit BlockMaxQuantizedIndex(const Config& config);
    BlockMaxQuantizedIndex();

    /**
     * Destructor
     */
    ~BlockMaxQuantizedIndex();

    /**
     * Build index from sparse documents
     */
    void build(const std::vector<SparseDoc>& documents);

    /**
     * Query the index
     * Returns top-k document IDs sorted by score (descending)
     */
    std::vector<doc_id_t> query(const SparseDoc& query,
                                 const QueryParams& params,
                                 QueryStats* stats = nullptr);

    /**
     * Get index statistics
     */
    size_t numDocuments() const { return num_documents_; }
    size_t numWindows() const { return num_windows_; }
    size_t numTerms() const { return num_terms_; }
    size_t memoryUsageBytes() const;

    /**
     * Direct document retrieval
     * Returns the sparse document by ID (from forward index)
     */
    const SparseDoc& getDocument(doc_id_t doc_id) const;

    /**
     * Batch document retrieval
     * Returns multiple documents by their IDs
     */
    std::vector<SparseDoc> getDocuments(const std::vector<doc_id_t>& doc_ids) const;

private:
    // Configuration
    Config config_;

    // Index metadata
    size_t num_documents_ = 0;
    size_t num_windows_ = 0;
    size_t num_terms_ = 0;

    // Quantized block structure
    struct QuantizedBlock {
        std::vector<doc_id_t> documents;  // Local doc IDs within window
    };

    // Inverted index: [term][block][window] -> QuantizedBlock
    std::vector<std::vector<std::vector<QuantizedBlock>>> quantized_index_;

    // Block sizes: [term][block] -> total doc count
    std::vector<std::vector<uint32_t>> block_sizes_;

    // Forward index: Original sparse documents for reranking
    std::vector<SparseDoc> forward_index_;

    // Quantization mapping: bin -> score
    std::vector<float> quant_values_;

    // Helper methods
    uint8_t quantizeScore(float score) const;
    float dequantizeScore(uint8_t bin) const;

    struct BlockWithScore {
        term_t term;
        uint8_t block_id;
        float gain;  // block_max_score * query_weight
        const std::vector<QuantizedBlock>* blocks;  // Pointer to blocks for this term-block

        BlockWithScore(term_t t, uint8_t bid, float g, const std::vector<QuantizedBlock>* b)
            : term(t), block_id(bid), gain(g), blocks(b) {}
    };

    // Query helper methods
    void selectBlocksAlphaMass(std::vector<BlockWithScore>& blocks,
                               float alpha,
                               size_t& selected_count);

    void selectBlocksMaxRatio(std::vector<BlockWithScore>& blocks,
                              float alpha,
                              size_t& selected_count);

    void scatterAdd(const std::vector<BlockWithScore>& blocks,
                   size_t selected_count,
                   std::vector<int32_t>& score_buf,
                   std::vector<std::pair<int32_t, doc_id_t>>& candidates,
                   size_t top_k_prime,
                   QueryStats* stats);

    void rerank(const std::vector<std::pair<int32_t, doc_id_t>>& candidates,
               const SparseDoc& query,
               std::vector<doc_id_t>& results,
               size_t top_k,
               QueryStats* stats);

    float dotProduct(const SparseDoc& query, const SparseDoc& doc) const;
};

} // namespace index
} // namespace diagon
