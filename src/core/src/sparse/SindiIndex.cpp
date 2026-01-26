// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/SindiIndex.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <queue>

namespace diagon {
namespace sparse {

// ==================== Construction ====================

SindiIndex::SindiIndex(const Config& config)
    : config_(config)
    , num_documents_(0)
    , num_postings_(0) {

    if (config_.block_size <= 0) {
        throw std::invalid_argument("block_size must be positive");
    }
    if (config_.chunk_power < 20 || config_.chunk_power > 40) {
        throw std::invalid_argument("chunk_power must be in [20, 40]");
    }
}

// ==================== Index Building ====================

void SindiIndex::build(const std::vector<SparseVector>& documents) {
    if (documents.empty()) {
        return;
    }

    num_documents_ = documents.size();

    // Step 1: Find vocabulary size (max dimension + 1)
    uint32_t max_dimension = 0;
    for (const auto& doc : documents) {
        if (!doc.empty()) {
            max_dimension = std::max(max_dimension, doc.maxDimension());
        }
    }
    config_.num_dimensions = max_dimension;

    // Step 2: Build inverted lists (collect postings for each term)
    // posting_lists[term] = [(doc_id, weight), ...]
    std::vector<std::vector<std::pair<uint32_t, float>>> posting_lists(max_dimension);

    for (uint32_t doc_id = 0; doc_id < documents.size(); ++doc_id) {
        const auto& doc = documents[doc_id];
        for (const auto& elem : doc) {
            if (elem.index < max_dimension) {
                posting_lists[elem.index].emplace_back(doc_id, elem.value);
            }
        }
    }

    // Step 3: Sort posting lists by document ID and divide into blocks
    term_doc_ids_.resize(max_dimension);
    term_weights_.resize(max_dimension);
    term_blocks_.resize(max_dimension);
    max_term_weights_.resize(max_dimension, 0.0f);

    for (uint32_t term = 0; term < max_dimension; ++term) {
        auto& postings = posting_lists[term];

        if (postings.empty()) {
            // Empty posting list
            term_doc_ids_[term] = columns::ColumnUInt32::create();
            term_weights_[term] = columns::ColumnFloat32::create();
            continue;
        }

        // Sort by document ID
        std::sort(postings.begin(), postings.end(),
                 [](const auto& a, const auto& b) { return a.first < b.first; });

        // Create ColumnVectors
        auto doc_ids_col = columns::ColumnUInt32::create();
        auto weights_col = columns::ColumnFloat32::create();

        doc_ids_col->getData().reserve(postings.size());
        weights_col->getData().reserve(postings.size());

        // Divide into blocks and compute metadata
        std::vector<BlockMetadata> blocks;
        size_t block_start = 0;

        while (block_start < postings.size()) {
            size_t block_end = std::min(block_start + config_.block_size,
                                       postings.size());

            // Find max weight in this block
            float max_weight = 0.0f;
            for (size_t i = block_start; i < block_end; ++i) {
                max_weight = std::max(max_weight, postings[i].second);
            }

            // Store block metadata
            blocks.emplace_back(
                static_cast<uint32_t>(block_start),
                static_cast<uint32_t>(block_end - block_start),
                max_weight
            );

            // Track maximum weight for this term
            max_term_weights_[term] = std::max(max_term_weights_[term], max_weight);

            block_start = block_end;
        }

        // Store postings in ColumnVectors
        for (const auto& [doc_id, weight] : postings) {
            doc_ids_col->getData().push_back(doc_id);
            weights_col->getData().push_back(weight);
        }

        term_doc_ids_[term] = doc_ids_col;
        term_weights_[term] = weights_col;
        term_blocks_[term] = std::move(blocks);

        num_postings_ += postings.size();
    }

    // Step 4: Build forward index (CSR format)
    forward_indptr_.resize(num_documents_ + 1, 0);
    forward_indices_.reserve(num_postings_);
    forward_values_.reserve(num_postings_);

    // Calculate offsets (indptr) by counting terms per document
    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        const auto& doc = documents[doc_id];
        // Count only terms within vocabulary
        uint32_t count = 0;
        for (const auto& elem : doc) {
            if (elem.index < max_dimension) {
                count++;
            }
        }
        forward_indptr_[doc_id + 1] = forward_indptr_[doc_id] + count;
    }

    // Fill indices and values by iterating through documents
    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        const auto& doc = documents[doc_id];
        for (const auto& elem : doc) {
            // Only include terms within vocabulary (same filter as inverted index)
            if (elem.index < max_dimension) {
                forward_indices_.push_back(elem.index);
                forward_values_.push_back(elem.value);
            }
        }
    }
}

// ==================== Persistence ====================

void SindiIndex::save(store::Directory* directory, const std::string& segment) {
    // TODO: Implement persistence
    // For Phase 3.1, focus on in-memory implementation
    // Phase 3.2 will add save/load with ColumnVector serialization
    (void)directory;
    (void)segment;
    throw std::runtime_error("SindiIndex::save() not yet implemented");
}

void SindiIndex::load(store::Directory* directory, const std::string& segment) {
    // TODO: Implement loading
    // For Phase 3.1, focus on in-memory implementation
    (void)directory;
    (void)segment;
    throw std::runtime_error("SindiIndex::load() not yet implemented");
}

// ==================== Search ====================

std::vector<SearchResult> SindiIndex::search(
    const SparseVector& query,
    int k) const
{
    if (k <= 0 || query.empty()) {
        return {};
    }

    if (config_.use_block_max) {
        return searchWithWand(query, k);
    } else {
        // Simple accumulation without WAND pruning
        std::vector<float> scores(num_documents_, 0.0f);

        for (const auto& query_elem : query) {
            uint32_t term = query_elem.index;
            float query_weight = query_elem.value;

            if (term >= term_doc_ids_.size() || !term_doc_ids_[term]) {
                continue;  // Term not in vocabulary
            }

            const auto& doc_ids_col = term_doc_ids_[term];
            const auto& weights_col = term_weights_[term];

            // Get raw pointers for SIMD processing
            const uint32_t* doc_ids = doc_ids_col->getData().data();
            const float* weights = weights_col->getData().data();
            size_t posting_count = doc_ids_col->size();

            // Accumulate scores with SIMD
            SindiScorer::accumulateScores(
                doc_ids,
                weights,
                posting_count,
                query_weight,
                scores,
                config_.use_simd,
                config_.use_prefetch
            );
        }

        // Extract top-k
        std::vector<SearchResult> results;
        results.reserve(num_documents_);

        for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
            if (scores[doc_id] > 0.0f) {
                results.emplace_back(doc_id, scores[doc_id]);
            }
        }

        // Sort by score descending and take top k
        std::partial_sort(results.begin(),
                         results.begin() + std::min(k, static_cast<int>(results.size())),
                         results.end());

        if (results.size() > static_cast<size_t>(k)) {
            results.resize(k);
        }

        return results;
    }
}

std::vector<SearchResult> SindiIndex::searchWithWand(
    const SparseVector& query,
    int k) const
{
    // Extract query terms and weights
    std::vector<uint32_t> query_terms;
    std::vector<float> query_weights;

    query_terms.reserve(query.size());
    query_weights.reserve(query.size());

    for (const auto& elem : query) {
        if (elem.index < term_doc_ids_.size() && term_doc_ids_[elem.index]) {
            query_terms.push_back(elem.index);
            query_weights.push_back(elem.value);
        }
    }

    if (query_terms.empty()) {
        return {};
    }

    // Initialize score accumulator
    std::vector<float> scores(num_documents_, 0.0f);

    // Min-heap for top-k results
    auto cmp = [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;  // Min-heap (lowest score at top)
    };
    std::priority_queue<SearchResult, std::vector<SearchResult>, decltype(cmp)> top_k(cmp);
    float threshold = 0.0f;  // WAND threshold

    // Process each query term
    for (size_t q = 0; q < query_terms.size(); ++q) {
        uint32_t term = query_terms[q];
        float query_weight = query_weights[q];

        const auto& blocks = term_blocks_[term];
        const auto& doc_ids_col = term_doc_ids_[term];
        const auto& weights_col = term_weights_[term];

        const uint32_t* doc_ids = doc_ids_col->getData().data();
        const float* weights = weights_col->getData().data();

        // Process blocks
        for (const auto& block : blocks) {
            // WAND pruning: skip block if upper bound can't improve top-k
            float upper_bound = query_weight * block.max_weight;
            if (top_k.size() >= static_cast<size_t>(k) && upper_bound < threshold) {
                continue;  // Skip this block
            }

            // Process block with SIMD
            SindiScorer::accumulateScores(
                &doc_ids[block.offset],
                &weights[block.offset],
                block.count,
                query_weight,
                scores,
                config_.use_simd,
                config_.use_prefetch
            );
        }
    }

    // Collect results with score > 0
    std::vector<SearchResult> results;
    results.reserve(num_documents_);

    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        if (scores[doc_id] > 0.0f) {
            results.emplace_back(doc_id, scores[doc_id]);
        }
    }

    // Sort and take top-k
    std::partial_sort(results.begin(),
                     results.begin() + std::min(k, static_cast<int>(results.size())),
                     results.end());

    if (results.size() > static_cast<size_t>(k)) {
        results.resize(k);
    }

    return results;
}

// ==================== Helpers ====================

float SindiIndex::computeUpperBound(
    const std::vector<uint32_t>& query_terms,
    const std::vector<float>& query_weights,
    size_t skip_term) const
{
    float upper_bound = 0.0f;

    for (size_t i = 0; i < query_terms.size(); ++i) {
        if (i == skip_term) {
            continue;  // Skip this term
        }

        uint32_t term = query_terms[i];
        float query_weight = query_weights[i];

        if (term < max_term_weights_.size()) {
            upper_bound += query_weight * max_term_weights_[term];
        }
    }

    return upper_bound;
}

// ==================== Forward Index ====================

SparseVector SindiIndex::getDocument(uint32_t doc_id) const {
    if (doc_id >= num_documents_) {
        throw std::out_of_range("Document ID " + std::to_string(doc_id) +
                               " out of range [0, " + std::to_string(num_documents_) + ")");
    }

    if (!hasForwardIndex()) {
        throw std::runtime_error("Forward index not available. Call build() first.");
    }

    // Get offsets from indptr
    uint32_t start = forward_indptr_[doc_id];
    uint32_t end = forward_indptr_[doc_id + 1];
    uint32_t num_terms = end - start;

    // Construct sparse vector from CSR format
    std::vector<SparseElement> elements;
    elements.reserve(num_terms);

    for (uint32_t i = start; i < end; ++i) {
        elements.emplace_back(forward_indices_[i], forward_values_[i]);
    }

    return SparseVector(elements);
}

void SindiIndex::prefetchDocument(uint32_t doc_id) const {
    if (doc_id >= num_documents_ || !hasForwardIndex()) {
        return;  // Silently ignore invalid prefetch
    }

    uint32_t offset = forward_indptr_[doc_id];

    // Prefetch indices and values arrays at the document's offset
    // Hint: 0 = read, 1 = temporal locality (will be used soon)
    __builtin_prefetch(&forward_indices_[offset], 0, 1);
    __builtin_prefetch(&forward_values_[offset], 0, 1);
}

}  // namespace sparse
}  // namespace diagon
