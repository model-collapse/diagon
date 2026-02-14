// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/columns/ColumnVector.h"
#include "diagon/sparse/SearchResult.h"
#include "diagon/sparse/SparseVector.h"
#include "diagon/store/Directory.h"
#include "diagon/store/MMapDirectory.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace sparse {

/**
 * QBlockIndex - Quantized block-based sparse vector index
 *
 * Implements efficient sparse vector search using block-based quantization
 * from "Accelerating Learned Sparse Indexes via Term Impact Decomposition"
 * (QBlock/BitQ paper).
 *
 * ## Key Concepts
 *
 * **Quantization**: Map float weights to discrete bins
 * - Default: 16 bins using uniform quantization
 * - Each bin represents a weight range with representative value
 * - Trade-off: Memory vs accuracy
 *
 * **Block Organization**: [term][block_id][window_id]
 * - block_id: Quantized weight bin (0-15)
 * - window_id: Document partition for cache locality
 * - Documents with similar weights grouped together
 *
 * **Gain-based Selection**: Blocks selected by potential contribution
 * - gain = quant_val[block_id] * query_weight
 * - Select blocks until α% of total mass reached
 * - More selective than exhaustive search
 *
 * **Window Partitioning**: Documents divided into fixed-size windows
 * - Default: 8192 docs per window
 * - Enables cache-friendly sequential access
 * - Parallel processing of windows
 *
 * ## Architecture
 *
 * **Storage**: ColumnVector format (same as SINDI)
 * - Zero-copy mmap access
 * - Optional compression (LZ4/ZSTD)
 * - Unified format for hybrid retrieval
 *
 * **Query Processing**: Gain-based block selection + ScatterAdd
 * 1. For each query term, compute gain for all blocks
 * 2. Select top blocks by gain until reaching α% mass
 * 3. ScatterAdd: accumulate scores from selected blocks
 * 4. Extract top-k results
 *
 * ## Example
 *
 * ```cpp
 * // Build index
 * QBlockIndex::Config config;
 * config.num_bins = 16;
 * config.window_size = 8192;
 * config.alpha = 0.75;  // Select blocks covering 75% of mass
 *
 * QBlockIndex index(config);
 * index.build(documents);
 * index.save(directory, "segment_0");
 *
 * // Search
 * auto results = index.search(query_vector, 10);  // Top 10
 * for (const auto& result : results) {
 *     std::cout << "Doc " << result.doc_id
 *               << " Score " << result.score << std::endl;
 * }
 * ```
 *
 * ## Comparison with SINDI
 *
 * | Feature | QBlock | SINDI |
 * |---------|--------|-------|
 * | Organization | By weight bins | By doc ID |
 * | Selection | Gain-based | Block-max WAND |
 * | Quantization | Weight bins | No quantization |
 * | Memory | Lower (quantized) | Higher (float) |
 * | Accuracy | ~98% (16 bins) | 100% (exact) |
 * | Speed | Faster (fewer blocks) | Moderate |
 *
 * **When to use QBlock**:
 * - Large indexes where memory is constrained
 * - Approximate results acceptable (~2% error)
 * - Query latency critical
 *
 * **When to use SINDI**:
 * - Exact results required
 * - Memory available for full-precision weights
 * - Smaller indexes
 */
class QBlockIndex {
public:
    // ==================== Configuration ====================

    /**
     * Index configuration
     */
    struct Config {
        /**
         * Number of quantization bins (default: 16)
         *
         * Trade-offs:
         * - More bins: Better accuracy, more memory, more blocks
         * - Fewer bins: Lower memory, faster search, less accurate
         *
         * Recommended: 8-32 (16 is good balance)
         */
        uint32_t num_bins = 16;

        /**
         * Documents per window (default: 8192)
         *
         * Windows enable cache-friendly sequential processing.
         *
         * Trade-offs:
         * - Larger windows: Fewer windows, less overhead
         * - Smaller windows: Better cache locality, more parallelism
         *
         * Recommended: 4096-16384 (8192 matches ClickHouse granules)
         */
        uint32_t window_size = 8192;

        /**
         * Block selection parameter (default: 0.75)
         *
         * Select blocks until reaching α% of total query mass.
         *
         * Trade-offs:
         * - Higher alpha: More blocks, better recall, slower
         * - Lower alpha: Fewer blocks, lower recall, faster
         *
         * Recommended: 0.5-0.9 (0.75 gives ~98% accuracy)
         */
        float alpha = 0.75f;

        /**
         * Selection mode (default: ALPHA_MASS)
         *
         * - ALPHA_MASS: Select until α% of total mass
         * - TOP_K: Select fixed number of top-gain blocks
         * - MAX_RATIO: Threshold by α * max_gain
         */
        enum SelectionMode {
            ALPHA_MASS,  // Default: best balance
            TOP_K,       // Fixed budget
            MAX_RATIO    // Threshold-based
        };
        SelectionMode selection_mode = ALPHA_MASS;

        /**
         * Fixed top-k for TOP_K mode (default: 100)
         */
        int fixed_top_k = 100;

        /**
         * Enable mmap for zero-copy access (default: true)
         */
        bool use_mmap = true;

        /**
         * Enable software prefetch (default: true)
         */
        bool use_prefetch = true;

        /**
         * MMap chunk size (2^chunk_power bytes, default: 30 = 1GB)
         */
        int chunk_power = 30;

        /**
         * Number of dimensions (set during build)
         */
        uint32_t num_dimensions = 0;
    };

    // ==================== Construction ====================

    /**
     * Construct QBlock index with configuration
     */
    explicit QBlockIndex(const Config& config);

    // ==================== Index Building ====================

    /**
     * Build quantized index from sparse vector documents
     *
     * Algorithm:
     * 1. Determine quantization bins from global weight distribution
     * 2. For each document:
     *    - For each term-weight pair:
     *      - Quantize weight to bin
     *      - Add doc to [term][bin][window]
     * 3. Compute representative values per bin (LUT)
     * 4. Store in ColumnVector format
     *
     * @param documents Input sparse vectors
     *
     * Time complexity: O(N × D) where N = num docs, D = avg dimensions per doc
     * Space complexity: O(N × D) but with quantized weights (1 byte vs 4 bytes)
     */
    void build(const std::vector<SparseVector>& documents);

    // ==================== Persistence ====================

    /**
     * Save index to directory
     *
     * File format (per segment):
     * - doc_ids_term_X_bin_Y.col: ColumnVector<uint32_t> for term X, bin Y
     * - quant_lut.bin: Representative values per bin
     * - quant_map.bin: Quantization map (256 → bin)
     * - block_stats.bin: Block sizes for each term/bin
     * - index_meta.bin: Config and statistics
     *
     * @param directory Output directory (FSDirectory for writing)
     * @param segment Segment name (e.g., "_0")
     */
    void save(store::Directory* directory, const std::string& segment);

    /**
     * Load index from directory
     *
     * Uses MMapDirectory if use_mmap=true, FSDirectory otherwise.
     *
     * @param directory Input directory (MMapDirectory for reading)
     * @param segment Segment name (e.g., "_0")
     */
    void load(store::Directory* directory, const std::string& segment);

    // ==================== Search ====================

    /**
     * Search for top-k similar documents
     *
     * Algorithm: Gain-based block selection + ScatterAdd
     * 1. For each query term:
     *    - Compute gain for each block: gain = quant_val[bin] * query_weight
     *    - Add to candidate list with gain
     * 2. Select blocks by gain (alpha-mass or top-k)
     * 3. ScatterAdd: For each window:
     *    - Accumulate scores from selected blocks
     *    - Extract top-k from window
     * 4. Global top-k merge
     *
     * @param query Sparse query vector
     * @param k Number of results to return
     * @return Vector of top-k results sorted by score (descending)
     *
     * Time complexity: O(k × log k + S) where S = selected blocks × window_size
     * Typical S << total docs due to block selection
     */
    std::vector<SearchResult> search(const SparseVector& query, int k) const;

    // ==================== Statistics ====================

    /**
     * Get index configuration
     */
    const Config& config() const { return config_; }

    /**
     * Get number of terms (dimensions) in vocabulary
     */
    uint32_t numTerms() const { return config_.num_dimensions; }

    /**
     * Get number of documents indexed
     */
    uint32_t numDocuments() const { return num_documents_; }

    /**
     * Get number of windows
     */
    uint32_t numWindows() const { return num_windows_; }

    /**
     * Get total number of postings
     */
    uint64_t numPostings() const { return num_postings_; }

    // ==================== Forward Index (Document Retrieval) ====================

    /**
     * Get document by ID
     *
     * Returns the sparse vector for a document using CSR format lookup.
     * Efficient O(1) access via indptr_ offset array.
     *
     * @param doc_id Document ID [0, num_documents)
     * @return Sparse vector for the document
     * @throws std::out_of_range if doc_id is invalid
     */
    SparseVector getDocument(uint32_t doc_id) const;

    /**
     * Prefetch document for cache optimization
     *
     * Hints CPU to load document data into cache before access.
     * Use before calling getDocument() in batch processing.
     *
     * @param doc_id Document ID to prefetch
     */
    void prefetchDocument(uint32_t doc_id) const;

    /**
     * Check if forward index is available
     *
     * Forward index is built during build() and loaded during load().
     * Required for getDocument() and prefetchDocument().
     */
    bool hasForwardIndex() const { return !forward_indptr_.empty(); }

private:
    // ==================== Configuration ====================

    Config config_;

    // ==================== Statistics ====================

    uint32_t num_documents_;  // Number of documents in index
    uint32_t num_windows_;    // Number of windows (ceil(docs / window_size))
    uint64_t num_postings_;   // Total postings across all terms/bins

    // ==================== Quantization ====================

    /**
     * Quantization map: float → bin
     * Maps 256 quantized uint8 values to bin IDs [0, num_bins)
     */
    std::vector<uint8_t> quant_map_;  // [256] → bin_id

    /**
     * Lookup table (LUT): Representative value per bin
     * Used for gain calculation: gain = quant_val[bin] * query_weight
     */
    std::vector<float> quant_val_;  // [num_bins] → representative weight

    // ==================== Posting Lists ====================

    /**
     * Block-organized posting lists: [term][bin][window]
     *
     * For each (term, bin) pair, store document IDs partitioned by window.
     * Documents with similar weights (same bin) grouped together.
     *
     * Storage: ColumnVector<uint32_t> for mmap support
     */
    std::vector<std::vector<std::vector<std::shared_ptr<columns::ColumnVector<uint32_t>>>>> blocks_;
    // blocks_[term][bin][window] = ColumnVector of doc IDs

    /**
     * Block sizes: [term][bin] → total docs in all windows
     * Used for gain-based selection to estimate block cost
     */
    std::vector<std::vector<uint32_t>> block_sizes_;

    // ==================== MMap Support ====================

    std::unique_ptr<store::MMapDirectory> mmap_dir_;

    // ==================== Forward Index (CSR Format) ====================

    /**
     * CSR indptr: Start/end offsets for each document
     *
     * Size: [num_documents + 1]
     * For document i:
     *   - Start offset: forward_indptr_[i]
     *   - End offset: forward_indptr_[i+1]
     *   - Number of terms: forward_indptr_[i+1] - forward_indptr_[i]
     */
    std::vector<uint32_t> forward_indptr_;

    /**
     * CSR indices: Term IDs (concatenated across all documents)
     *
     * Size: [total_postings]
     * For document i: forward_indices_[forward_indptr_[i] : forward_indptr_[i+1]]
     */
    std::vector<uint32_t> forward_indices_;

    /**
     * CSR values: Weights (parallel to indices)
     *
     * Size: [total_postings]
     * For document i: forward_values_[forward_indptr_[i] : forward_indptr_[i+1]]
     */
    std::vector<float> forward_values_;

    // ==================== Helper Methods ====================

    /**
     * Quantize a float weight to bin ID
     *
     * Algorithm:
     * 1. Clamp weight to [0, max_weight]
     * 2. Scale to [0, 255]: uint8 = (weight / max_weight) * 255
     * 3. Map to bin: bin = quant_map[uint8]
     *
     * @param weight Original float weight
     * @return Bin ID [0, num_bins)
     */
    uint8_t quantizeWeight(float weight) const;

    /**
     * Build quantization map and LUT from weight distribution
     *
     * Algorithm:
     * 1. Collect all weights from documents
     * 2. Sort weights
     * 3. Divide into num_bins equal-frequency bins
     * 4. Compute representative value (mean) per bin
     * 5. Create quant_map[256] for fast lookup
     *
     * @param documents Input sparse vectors
     */
    void buildQuantization(const std::vector<SparseVector>& documents);

    /**
     * Compute gain for all blocks of a query term
     *
     * @param query_weight Weight of query term
     * @param term_blocks Blocks for this term
     * @param term Term ID
     * @param gains Output: gains[bin] = quant_val[bin] * query_weight
     */
    void computeBlockGains(float query_weight, uint32_t term, std::vector<float>& gains) const;
};

}  // namespace sparse
}  // namespace diagon
