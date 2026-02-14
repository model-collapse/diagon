// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/QBlockIndex.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <queue>
#include <stdexcept>

namespace diagon {
namespace sparse {

// ==================== Construction ====================

QBlockIndex::QBlockIndex(const Config& config)
    : config_(config)
    , num_documents_(0)
    , num_windows_(0)
    , num_postings_(0) {
    if (config_.num_bins == 0 || config_.num_bins > 256) {
        throw std::invalid_argument("num_bins must be in [1, 256]");
    }
    if (config_.window_size == 0) {
        throw std::invalid_argument("window_size must be positive");
    }
    if (config_.alpha < 0.0f || config_.alpha > 1.0f) {
        throw std::invalid_argument("alpha must be in [0, 1]");
    }
    if (config_.chunk_power < 20 || config_.chunk_power > 40) {
        throw std::invalid_argument("chunk_power must be in [20, 40]");
    }
}

// ==================== Quantization ====================

void QBlockIndex::buildQuantization(const std::vector<SparseVector>& documents) {
    // Step 1: Collect all weights
    std::vector<float> all_weights;
    all_weights.reserve(num_postings_);  // Rough estimate

    for (const auto& doc : documents) {
        for (const auto& elem : doc) {
            if (elem.value > 0.0f) {
                all_weights.push_back(elem.value);
            }
        }
    }

    if (all_weights.empty()) {
        // Empty index, create default quantization
        quant_map_.resize(256, 0);
        quant_val_.resize(config_.num_bins, 0.0f);
        return;
    }

    // Step 2: Sort weights
    std::sort(all_weights.begin(), all_weights.end());

    // Step 3: Divide into equal-frequency bins
    // Each bin should contain ~equal number of weights
    size_t weights_per_bin = all_weights.size() / config_.num_bins;
    if (weights_per_bin == 0)
        weights_per_bin = 1;

    quant_val_.resize(config_.num_bins);
    std::vector<float> bin_boundaries(config_.num_bins + 1);

    bin_boundaries[0] = 0.0f;
    for (uint32_t bin = 0; bin < config_.num_bins; ++bin) {
        size_t start_idx = bin * weights_per_bin;
        size_t end_idx = (bin == config_.num_bins - 1)
                             ? all_weights.size()
                             : std::min((bin + 1) * weights_per_bin, all_weights.size());

        // Representative value = mean of weights in bin
        float sum = 0.0f;
        for (size_t i = start_idx; i < end_idx; ++i) {
            sum += all_weights[i];
        }
        quant_val_[bin] = sum / (end_idx - start_idx);

        // Boundary = max weight in bin
        bin_boundaries[bin + 1] = all_weights[end_idx - 1];
    }
    bin_boundaries[config_.num_bins] = all_weights.back() * 1.001f;  // Ensure last bin catches max

    // Step 4: Create quantization map [0, 255] → bin
    quant_map_.resize(256);
    float max_weight = all_weights.back();

    for (int i = 0; i < 256; ++i) {
        // Map quantized uint8 back to float
        float weight = (i / 255.0f) * max_weight;

        // Find bin for this weight
        uint8_t bin = 0;
        for (uint32_t b = 0; b < config_.num_bins; ++b) {
            if (weight >= bin_boundaries[b] && weight < bin_boundaries[b + 1]) {
                bin = static_cast<uint8_t>(b);
                break;
            }
        }
        quant_map_[i] = bin;
    }
}

uint8_t QBlockIndex::quantizeWeight(float weight) const {
    if (quant_map_.empty()) {
        return 0;  // Not initialized
    }

    // Clamp and scale to [0, 255]
    float max_weight = 3.0f;  // Same as BitQ
    uint8_t quantized = static_cast<uint8_t>(
        std::min(255.0f, std::max(0.0f, (weight / max_weight) * 255.0f)));

    // Map to bin
    return quant_map_[quantized];
}

// ==================== Index Building ====================

void QBlockIndex::build(const std::vector<SparseVector>& documents) {
    if (documents.empty()) {
        return;
    }

    num_documents_ = documents.size();
    num_windows_ = (num_documents_ + config_.window_size - 1) / config_.window_size;

    // Step 1: Find vocabulary size
    uint32_t max_dimension = 0;
    for (const auto& doc : documents) {
        if (!doc.empty()) {
            max_dimension = std::max(max_dimension, doc.maxDimension());
        }
    }
    config_.num_dimensions = max_dimension;

    // Step 2: Build quantization (analyze weight distribution)
    // First pass: count postings for reservation
    num_postings_ = 0;
    for (const auto& doc : documents) {
        num_postings_ += doc.size();
    }

    buildQuantization(documents);

    // Step 3: Initialize block structure [term][bin][window]
    blocks_.resize(max_dimension);
    block_sizes_.resize(max_dimension);

    for (uint32_t term = 0; term < max_dimension; ++term) {
        blocks_[term].resize(config_.num_bins);
        block_sizes_[term].resize(config_.num_bins, 0);

        for (uint32_t bin = 0; bin < config_.num_bins; ++bin) {
            blocks_[term][bin].resize(num_windows_);
            for (uint32_t window = 0; window < num_windows_; ++window) {
                blocks_[term][bin][window] = columns::ColumnUInt32::create();
            }
        }
    }

    // Step 4: Insert documents into blocks
    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        const auto& doc = documents[doc_id];
        uint32_t window_id = doc_id / config_.window_size;
        uint32_t local_doc_id = doc_id % config_.window_size;

        for (const auto& elem : doc) {
            uint32_t term = elem.index;
            float weight = elem.value;

            if (term >= max_dimension)
                continue;
            if (weight <= 0.0f)
                continue;

            // Quantize weight to bin
            uint8_t bin = quantizeWeight(weight);

            // Add to block [term][bin][window]
            blocks_[term][bin][window_id]->getData().push_back(local_doc_id);
            block_sizes_[term][bin]++;
        }
    }

    // Step 5: Sort document IDs within each block for cache efficiency
    for (uint32_t term = 0; term < max_dimension; ++term) {
        for (uint32_t bin = 0; bin < config_.num_bins; ++bin) {
            for (uint32_t window = 0; window < num_windows_; ++window) {
                auto& doc_ids = blocks_[term][bin][window]->getData();
                std::sort(doc_ids.begin(), doc_ids.end());
            }
        }
    }

    // Step 6: Build forward index (CSR format)
    forward_indptr_.resize(num_documents_ + 1, 0);
    forward_indices_.reserve(num_postings_);
    forward_values_.reserve(num_postings_);

    // Calculate offsets (indptr) by counting terms per document
    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        const auto& doc = documents[doc_id];
        // Count only valid terms (same filters as inverted index building)
        uint32_t count = 0;
        for (const auto& elem : doc) {
            if (elem.index < max_dimension && elem.value > 0.0f) {
                count++;
            }
        }
        forward_indptr_[doc_id + 1] = forward_indptr_[doc_id] + count;
    }

    // Fill indices and values by iterating through documents
    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        const auto& doc = documents[doc_id];
        for (const auto& elem : doc) {
            // Only include valid terms (same filters as inverted index building)
            if (elem.index < max_dimension && elem.value > 0.0f) {
                forward_indices_.push_back(elem.index);
                forward_values_.push_back(elem.value);
            }
        }
    }
}

// ==================== Persistence ====================

void QBlockIndex::save(store::Directory* directory, const std::string& segment) {
    // TODO: Implement persistence
    // Format:
    // - For each term, bin: save ColumnVector to doc_ids_term_X_bin_Y.col
    // - Save quant_map_, quant_val_, block_sizes_
    // - Save config and statistics
    (void)directory;
    (void)segment;
    throw std::runtime_error("QBlockIndex::save() not yet implemented");
}

void QBlockIndex::load(store::Directory* directory, const std::string& segment) {
    // TODO: Implement loading
    (void)directory;
    (void)segment;
    throw std::runtime_error("QBlockIndex::load() not yet implemented");
}

// ==================== Search ====================

void QBlockIndex::computeBlockGains(float query_weight, uint32_t term,
                                    std::vector<float>& gains) const {
    gains.resize(config_.num_bins);

    for (uint32_t bin = 0; bin < config_.num_bins; ++bin) {
        // Gain = representative_value * query_weight
        gains[bin] = quant_val_[bin] * query_weight;
    }
}

std::vector<SearchResult> QBlockIndex::search(const SparseVector& query, int k) const {
    if (k <= 0 || query.empty()) {
        return {};
    }

    // Step 1: Collect all candidate blocks with gains
    struct BlockCandidate {
        const std::vector<std::shared_ptr<columns::ColumnVector<uint32_t>>>*
            windows;  // Pointer to all windows
        uint32_t term;
        uint32_t bin;
        int32_t gain;  // Use int32 like reference code
        float weight;

        BlockCandidate(const std::vector<std::shared_ptr<columns::ColumnVector<uint32_t>>>* w,
                       uint32_t t, uint32_t b, int32_t g, float wt)
            : windows(w)
            , term(t)
            , bin(b)
            , gain(g)
            , weight(wt) {}

        bool operator<(const BlockCandidate& other) const {
            return gain < other.gain;  // Min-heap (invert for max)
        }
    };

    std::vector<BlockCandidate> candidates;
    candidates.reserve(query.size() * config_.num_bins);

    float total_mass = 0.0f;

    // Build candidate list with gains
    for (const auto& query_elem : query) {
        uint32_t term = query_elem.index;
        float query_weight = query_elem.value;

        if (term >= blocks_.size())
            continue;

        const auto& term_blocks = blocks_[term];
        const auto& term_block_sizes = block_sizes_[term];

        for (uint32_t bin = 0; bin < config_.num_bins; ++bin) {
            // Skip empty blocks
            if (term_block_sizes[bin] == 0)
                continue;

            // Gain calculation (int32 like reference, scaled by 1000 for precision)
            constexpr float GAIN_SCALE = 1000.0f;
            int32_t gain = static_cast<int32_t>(quant_val_[bin] * query_weight * GAIN_SCALE);
            float weight = quant_val_[bin] * query_weight;  // Unscaled for selection

            candidates.emplace_back(&term_blocks[bin], term, bin, gain, weight);
            total_mass += weight;
        }
    }

    if (candidates.empty()) {
        return {};
    }

    // Step 2: Select blocks based on mode (returns iterators like reference)
    auto end_it = candidates.end();

    if (config_.selection_mode == Config::TOP_K) {
        // Select fixed top-k blocks by gain
        int num_select = std::min(config_.fixed_top_k, static_cast<int>(candidates.size()));
        auto cmp = [](const auto& a, const auto& b) {
            return a.gain > b.gain;
        };
        if (num_select > 0) {
            std::partial_sort(candidates.begin(), candidates.begin() + num_select, candidates.end(),
                              cmp);
            end_it = candidates.begin() + num_select;
        }

    } else if (config_.selection_mode == Config::MAX_RATIO) {
        // Threshold by alpha * max_weight (use unscaled weight, not scaled gain)
        float max_weight = 0.0f;
        for (const auto& cand : candidates) {
            max_weight = std::max(max_weight, cand.weight);
        }
        float threshold = max_weight * config_.alpha;

        // Partition: selected blocks at front
        end_it = std::partition(candidates.begin(), candidates.end(),
                                [threshold](const auto& c) { return c.weight >= threshold; });

    } else {  // ALPHA_MASS (default)
        // Select blocks until reaching alpha% of total mass (like reference)
        float target_mass = total_mass * config_.alpha;

        // Sort by gain descending
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.gain > b.gain; });

        // Select until reaching target mass
        float current_mass = 0.0f;
        end_it = candidates.begin();
        for (auto it = candidates.begin(); it != candidates.end(); ++it) {
            current_mass += it->weight;
            end_it = it + 1;
            if (current_mass >= target_mass)
                break;
        }
    }

    // Step 3: ScatterAdd - window by window processing (like reference)
    std::vector<int32_t> score_buf(num_documents_, 0);  // Use int32 like reference
    int32_t* __restrict buf = score_buf.data();         // Restrict pointer

    // Result collection
    std::vector<SearchResult> results;
    results.reserve(num_documents_);

    // Touched blocks tracking for cleanup
    std::vector<const columns::ColumnVector<uint32_t>*> touched_blocks;
    touched_blocks.reserve((end_it - candidates.begin()) + 2);

    constexpr size_t PF = 48;  // Prefetch distance

    for (uint32_t window_id = 0; window_id < num_windows_; ++window_id) {
        uint32_t window_offset = window_id * config_.window_size;

        // Part 1: Score accumulation
        for (auto block_it = candidates.begin(); block_it != end_it; ++block_it) {
            const auto& block_col = (*block_it->windows)[window_id];
            const auto& docs = block_col->getData();

            if (docs.empty())
                continue;

            touched_blocks.push_back(block_col.get());

            // Prefetch next block's data
            if (config_.use_prefetch && (block_it + 1) != end_it) {
                const auto& next_block_col = (*(block_it + 1)->windows)[window_id];
                if (!next_block_col->getData().empty()) {
                    __builtin_prefetch(next_block_col->getData().data(), 0, 1);
                }
            }

            const size_t n = docs.size();
            const int32_t gain = block_it->gain;

            // Prefetch first PF elements
            size_t pf_count = std::min(n, PF);
            if (config_.use_prefetch) {
                for (size_t p = 0; p < pf_count; ++p) {
                    __builtin_prefetch(&buf[window_offset + docs[p]], 1, 0);
                }
            }

            // Main loop: prefetch ahead while processing
            size_t i = 0;
            for (; i + PF < n; ++i) {
                if (config_.use_prefetch) {
                    __builtin_prefetch(&buf[window_offset + docs[i + PF]], 1, 0);
                }
                buf[window_offset + docs[i]] += gain;
            }

            // Tail loop
            for (; i < n; ++i) {
                buf[window_offset + docs[i]] += gain;
            }
        }

        // Part 2: Collect scores and reset (score_buf already hot from Part 1)
        constexpr float GAIN_SCALE = 1000.0f;
        for (const auto* block_col : touched_blocks) {
            for (uint32_t local_doc_id : block_col->getData()) {
                uint32_t global_doc_id = window_offset + local_doc_id;
                int32_t score = buf[global_doc_id];
                if (score > 0) {
                    // Scale back to float
                    results.emplace_back(global_doc_id, static_cast<float>(score) / GAIN_SCALE);
                }
                buf[global_doc_id] = 0;  // Reset after collection
            }
        }

        touched_blocks.clear();
    }

    // Step 4: Sort and take top-k

    // Sort by score descending and take top-k
    std::partial_sort(results.begin(),
                      results.begin() + std::min(k, static_cast<int>(results.size())),
                      results.end());

    if (results.size() > static_cast<size_t>(k)) {
        results.resize(k);
    }

    return results;
}

// ==================== Forward Index ====================

SparseVector QBlockIndex::getDocument(uint32_t doc_id) const {
    if (doc_id >= num_documents_) {
        throw std::out_of_range("Document ID " + std::to_string(doc_id) + " out of range [0, " +
                                std::to_string(num_documents_) + ")");
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

void QBlockIndex::prefetchDocument(uint32_t doc_id) const {
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
