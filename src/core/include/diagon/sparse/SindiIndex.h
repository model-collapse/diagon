// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/columns/ColumnVector.h"
#include "diagon/sparse/SindiScorer.h"
#include "diagon/sparse/SparseVector.h"
#include "diagon/store/Directory.h"
#include "diagon/store/MMapDirectory.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace sparse {

/**
 * Search result for sparse vector queries
 */
struct SearchResult {
    uint32_t doc_id;  // Document ID
    float score;      // Similarity score

    SearchResult() : doc_id(0), score(0.0f) {}  // Default constructor
    SearchResult(uint32_t id, float s) : doc_id(id), score(s) {}

    // Sort by score descending
    bool operator<(const SearchResult& other) const {
        return score > other.score;  // Higher score first
    }
};

/**
 * SindiIndex - SIMD-optimized sparse vector index
 *
 * Implements efficient sparse vector search using:
 * - Block-max WAND pruning (skip blocks that can't contribute)
 * - AVX2 SIMD score accumulation (8× parallelism)
 * - Software prefetch (reduce cache misses)
 * - ColumnVector storage (mmap support, compression)
 *
 * Based on SINDI paper: "SINDI: Efficient Inverted Index Using Block-Max SIMD"
 * (https://arxiv.org/html/2509.08395v2)
 *
 * ## Architecture
 *
 * **Storage**: Posting lists stored in ColumnVector format
 * - Zero-copy mmap access via MMapDirectory
 * - Optional compression (LZ4/ZSTD)
 * - Unified format with QBlock
 *
 * **Organization**: Block-based posting lists
 * - Each term divided into fixed-size blocks (default: 128 docs)
 * - Precomputed max weight per block for WAND pruning
 * - Sequential access within blocks for cache efficiency
 *
 * **Query Processing**: Block-max WAND algorithm
 * 1. Compute upper bound score for each term's blocks
 * 2. Process blocks in sorted order by upper bound
 * 3. Skip blocks that can't improve top-k
 * 4. SIMD accumulation within selected blocks
 *
 * ## Example
 *
 * ```cpp
 * // Build index
 * SindiIndex::Config config;
 * config.block_size = 128;
 * config.use_mmap = true;
 * config.use_prefetch = true;
 *
 * SindiIndex index(config);
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
 */
class SindiIndex {
public:
    // ==================== Configuration ====================

    /**
     * Index configuration
     */
    struct Config {
        /**
         * Documents per block (default: 128)
         *
         * Trade-offs:
         * - Smaller blocks: Better WAND pruning, more metadata overhead
         * - Larger blocks: Fewer blocks to process, less effective pruning
         *
         * Recommended: 64-256 depending on dataset
         */
        int block_size = 128;

        /**
         * Enable block-max WAND optimization (default: true)
         *
         * Precompute max weight per block to skip blocks early.
         * Slight overhead during indexing, significant speedup during search.
         */
        bool use_block_max = true;

        /**
         * Enable SIMD acceleration (default: true)
         *
         * Use AVX2 for 8× parallel score accumulation.
         * Falls back to scalar if AVX2 not available.
         */
        bool use_simd = true;

        /**
         * Enable memory mapping (default: true)
         *
         * Load posting lists via mmap for zero-copy access.
         * Recommended for large indexes (>1GB).
         * Disable for small indexes or memory-constrained systems.
         */
        bool use_mmap = true;

        /**
         * Enable software prefetch (default: true)
         *
         * Hint CPU to load next cache line during processing.
         * Reduces cache miss penalty (~2-4× speedup on sequential scans).
         */
        bool use_prefetch = true;

        /**
         * MMap chunk size power (default: 30 = 1GB)
         *
         * Only used if use_mmap=true.
         * Chunk size = 2^chunk_power bytes.
         * Range: [20, 40] = [1MB, 1TB]
         */
        int chunk_power = 30;

        /**
         * Number of dimensions (terms) in vocabulary
         *
         * Set automatically during build().
         * Can be set manually for preallocation.
         */
        uint32_t num_dimensions = 0;
    };

    // ==================== Block Metadata ====================

    /**
     * Metadata for one posting list block
     *
     * Stored separately from posting data for efficient WAND pruning.
     */
    struct BlockMetadata {
        uint32_t offset;      // Offset in doc_ids/weights arrays
        uint32_t count;       // Number of documents in block
        float max_weight;     // Maximum weight in block (for WAND)

        BlockMetadata() : offset(0), count(0), max_weight(0.0f) {}
        BlockMetadata(uint32_t off, uint32_t cnt, float max_w)
            : offset(off), count(cnt), max_weight(max_w) {}
    };

    // ==================== Construction ====================

    /**
     * Create index with configuration
     *
     * @param config Index configuration (block size, SIMD, mmap, etc.)
     */
    explicit SindiIndex(const Config& config);

    /**
     * Default destructor
     */
    ~SindiIndex() = default;

    // ==================== Index Building ====================

    /**
     * Build index from sparse vectors
     *
     * Constructs inverted index from document vectors:
     * 1. Collect postings for each term (dimension)
     * 2. Sort postings by document ID
     * 3. Divide into fixed-size blocks
     * 4. Compute block metadata (max weight per block)
     * 5. Store in ColumnVector format
     *
     * @param documents Vector of sparse document vectors
     *
     * Time complexity: O(N × D) where N = num docs, D = avg dimensions per doc
     * Space complexity: O(N × D) for posting lists
     */
    void build(const std::vector<SparseVector>& documents);

    // ==================== Persistence ====================

    /**
     * Save index to directory
     *
     * File format (per segment):
     * - doc_ids_term_X.col: ColumnVector<uint32_t> for term X
     * - weights_term_X.col: ColumnVector<float> for term X
     * - block_meta.bin: Block metadata for all terms
     * - term_stats.bin: Max weight per term
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
     * Algorithm: Block-max WAND
     * 1. For each query term, maintain cursor in posting list
     * 2. Compute upper bound score for each term's current block
     * 3. Skip blocks that can't improve top-k (WAND threshold)
     * 4. Accumulate scores for promising blocks using SIMD
     * 5. Update top-k heap
     *
     * @param query Sparse query vector
     * @param k Number of results to return
     * @return Vector of top-k results sorted by score (descending)
     *
     * Time complexity: O(k × log k + B) where B = blocks processed
     * Typical B << total blocks due to WAND pruning
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
     * Get total number of postings (sum of posting list lengths)
     */
    uint64_t numPostings() const { return num_postings_; }

private:
    // ==================== Configuration ====================

    Config config_;

    // ==================== Statistics ====================

    uint32_t num_documents_;  // Number of documents in index
    uint64_t num_postings_;   // Total postings across all terms

    // ==================== Posting Lists ====================

    /**
     * Posting lists stored as ColumnVectors (mmap-able)
     *
     * One ColumnVector per term containing all document IDs.
     * Sorted by document ID within each term.
     */
    std::vector<std::shared_ptr<columns::ColumnVector<uint32_t>>> term_doc_ids_;

    /**
     * Term weights for each posting
     *
     * Parallel to term_doc_ids_: weights[term][i] is weight for doc_ids[term][i]
     */
    std::vector<std::shared_ptr<columns::ColumnVector<float>>> term_weights_;

    // ==================== Block Metadata ====================

    /**
     * Block metadata for WAND pruning
     *
     * term_blocks_[term][block_idx] contains metadata for block
     */
    std::vector<std::vector<BlockMetadata>> term_blocks_;

    /**
     * Maximum weight per term (for WAND upper bound)
     */
    std::vector<float> max_term_weights_;

    // ==================== MMap Support ====================

    /**
     * Memory-mapped directory (nullptr if use_mmap=false)
     */
    std::unique_ptr<store::MMapDirectory> mmap_dir_;

    // ==================== Query Processing ====================

    /**
     * Search with block-max WAND algorithm
     *
     * @param query Sparse query vector
     * @param k Number of results
     * @return Top-k results
     */
    std::vector<SearchResult> searchWithWand(
        const SparseVector& query,
        int k) const;

    /**
     * Compute WAND upper bound score
     *
     * Sum of (query_weight × max_term_weight) for terms excluding skip_term.
     * Used to determine if a document can improve top-k.
     *
     * @param query_terms Query term indices
     * @param query_weights Query term weights
     * @param skip_term Term to exclude from upper bound
     * @return Upper bound score
     */
    float computeUpperBound(
        const std::vector<uint32_t>& query_terms,
        const std::vector<float>& query_weights,
        size_t skip_term) const;
};

}  // namespace sparse
}  // namespace diagon
