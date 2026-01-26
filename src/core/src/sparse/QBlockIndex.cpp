// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/QBlockIndex.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <queue>

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
    if (weights_per_bin == 0) weights_per_bin = 1;

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
        std::min(255.0f, std::max(0.0f, (weight / max_weight) * 255.0f))
    );

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

            if (term >= max_dimension) continue;
            if (weight <= 0.0f) continue;

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

void QBlockIndex::computeBlockGains(
    float query_weight,
    uint32_t term,
    std::vector<float>& gains) const
{
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
        uint32_t term;
        uint32_t bin;
        float gain;
        float weight;

        BlockCandidate(uint32_t t, uint32_t b, float g, float w)
            : term(t), bin(b), gain(g), weight(w) {}

        bool operator<(const BlockCandidate& other) const {
            return gain < other.gain;  // Min-heap (invert for max)
        }
    };

    std::vector<BlockCandidate> candidates;
    candidates.reserve(query.size() * config_.num_bins);

    float total_mass = 0.0f;

    for (const auto& query_elem : query) {
        uint32_t term = query_elem.index;
        float query_weight = query_elem.value;

        if (term >= blocks_.size()) continue;

        // Compute gains for all bins of this term
        std::vector<float> gains;
        computeBlockGains(query_weight, term, gains);

        for (uint32_t bin = 0; bin < config_.num_bins; ++bin) {
            // Skip empty blocks
            if (block_sizes_[term][bin] == 0) continue;

            float gain = gains[bin];
            float weight = gain;  // Same for now

            candidates.emplace_back(term, bin, gain, weight);
            total_mass += weight;
        }
    }

    if (candidates.empty()) {
        return {};
    }

    // Step 2: Select blocks based on mode
    std::vector<BlockCandidate> selected_blocks;

    if (config_.selection_mode == Config::TOP_K) {
        // Select fixed top-k blocks by gain
        int num_select = std::min(config_.fixed_top_k, static_cast<int>(candidates.size()));
        std::partial_sort(candidates.begin(),
                         candidates.begin() + num_select,
                         candidates.end(),
                         [](const auto& a, const auto& b) { return a.gain > b.gain; });
        selected_blocks.assign(candidates.begin(), candidates.begin() + num_select);

    } else if (config_.selection_mode == Config::MAX_RATIO) {
        // Threshold by alpha * max_gain
        float max_gain = 0.0f;
        for (const auto& cand : candidates) {
            max_gain = std::max(max_gain, cand.gain);
        }
        float threshold = max_gain * config_.alpha;

        for (const auto& cand : candidates) {
            if (cand.gain >= threshold) {
                selected_blocks.push_back(cand);
            }
        }

    } else {  // ALPHA_MASS (default)
        // Select blocks until reaching alpha% of total mass
        float target_mass = total_mass * config_.alpha;

        // Sort by gain descending
        std::sort(candidates.begin(), candidates.end(),
                 [](const auto& a, const auto& b) { return a.gain > b.gain; });

        float current_mass = 0.0f;
        for (const auto& cand : candidates) {
            selected_blocks.push_back(cand);
            current_mass += cand.weight;
            if (current_mass >= target_mass) break;
        }
    }

    // Step 3: ScatterAdd - accumulate scores window by window
    std::vector<float> score_buf(num_documents_, 0.0f);

    for (uint32_t window_id = 0; window_id < num_windows_; ++window_id) {
        uint32_t window_offset = window_id * config_.window_size;
        uint32_t window_end = std::min(window_offset + config_.window_size, num_documents_);

        // Part 1: Score accumulation
        for (const auto& block : selected_blocks) {
            const auto& doc_ids_col = blocks_[block.term][block.bin][window_id];
            const auto& doc_ids = doc_ids_col->getData();

            if (doc_ids.empty()) continue;

            float gain = block.gain;

            // Prefetch-optimized scatter-add
            constexpr size_t PREFETCH_DISTANCE = 48;
            const size_t n = doc_ids.size();

            // Prefetch first batch
            size_t pf_count = std::min(n, PREFETCH_DISTANCE);
            if (config_.use_prefetch) {
                for (size_t p = 0; p < pf_count; ++p) {
                    uint32_t global_doc_id = window_offset + doc_ids[p];
                    __builtin_prefetch(&score_buf[global_doc_id], 1, 0);
                }
            }

            // Main loop with prefetch
            size_t i = 0;
            for (; i + PREFETCH_DISTANCE < n; ++i) {
                if (config_.use_prefetch) {
                    uint32_t pf_doc_id = window_offset + doc_ids[i + PREFETCH_DISTANCE];
                    __builtin_prefetch(&score_buf[pf_doc_id], 1, 0);
                }

                uint32_t global_doc_id = window_offset + doc_ids[i];
                score_buf[global_doc_id] += gain;
            }

            // Tail loop
            for (; i < n; ++i) {
                uint32_t global_doc_id = window_offset + doc_ids[i];
                score_buf[global_doc_id] += gain;
            }
        }
    }

    // Step 4: Extract top-k results
    std::vector<SearchResult> results;
    results.reserve(num_documents_);

    for (uint32_t doc_id = 0; doc_id < num_documents_; ++doc_id) {
        if (score_buf[doc_id] > 0.0f) {
            results.emplace_back(doc_id, score_buf[doc_id]);
        }
    }

    // Sort by score descending and take top-k
    std::partial_sort(results.begin(),
                     results.begin() + std::min(k, static_cast<int>(results.size())),
                     results.end());

    if (results.size() > static_cast<size_t>(k)) {
        results.resize(k);
    }

    return results;
}

}  // namespace sparse
}  // namespace diagon
