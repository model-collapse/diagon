// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/BlockMaxQuantizedIndex.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <unordered_map>

namespace diagon {
namespace index {

BlockMaxQuantizedIndex::BlockMaxQuantizedIndex()
    : BlockMaxQuantizedIndex(Config()) {
}

BlockMaxQuantizedIndex::BlockMaxQuantizedIndex(const Config& config)
    : config_(config) {
    // Initialize quantization values
    quant_values_.resize(config_.num_quantization_bins);
    for (size_t i = 0; i < config_.num_quantization_bins; ++i) {
        quant_values_[i] = (static_cast<float>(i) / 255.0f) * config_.max_score;
    }
}

BlockMaxQuantizedIndex::~BlockMaxQuantizedIndex() = default;

uint8_t BlockMaxQuantizedIndex::quantizeScore(float score) const {
    // Clamp to [0, max_score]
    score = std::max(0.0f, std::min(config_.max_score, score));

    // Map to [0, 255]
    uint8_t bin = static_cast<uint8_t>((score / config_.max_score) * 255.0f);
    return bin;
}

float BlockMaxQuantizedIndex::dequantizeScore(uint8_t bin) const {
    return quant_values_[bin];
}

void BlockMaxQuantizedIndex::build(const std::vector<SparseDoc>& documents) {
    num_documents_ = documents.size();
    num_windows_ = (num_documents_ + config_.window_size - 1) / config_.window_size;

    // Find max term ID
    num_terms_ = 0;
    for (const auto& doc : documents) {
        for (const auto& elem : doc) {
            num_terms_ = std::max(num_terms_, static_cast<size_t>(elem.term + 1));
        }
    }

    // Initialize index structure: [term][block][window]
    quantized_index_.resize(num_terms_);
    block_sizes_.resize(num_terms_);

    for (size_t term = 0; term < num_terms_; ++term) {
        quantized_index_[term].resize(config_.num_quantization_bins);
        block_sizes_[term].resize(config_.num_quantization_bins, 0);

        for (size_t block = 0; block < config_.num_quantization_bins; ++block) {
            quantized_index_[term][block].resize(num_windows_);
        }
    }

    // Build inverted index
    for (size_t doc_id = 0; doc_id < documents.size(); ++doc_id) {
        size_t window_id = doc_id / config_.window_size;
        doc_id_t local_doc_id = doc_id % config_.window_size;

        const auto& doc = documents[doc_id];
        for (const auto& elem : doc) {
            term_t term = elem.term;
            float score = elem.score;

            // Quantize score to block
            uint8_t block_id = quantizeScore(score);

            // Add to inverted index
            quantized_index_[term][block_id][window_id].documents.push_back(local_doc_id);
            block_sizes_[term][block_id]++;
        }
    }

    // Store forward index for reranking
    forward_index_ = documents;
}

std::vector<doc_id_t> BlockMaxQuantizedIndex::query(const SparseDoc& query,
                                                      const QueryParams& params,
                                                      QueryStats* stats) {
    auto start = std::chrono::high_resolution_clock::now();

    // Statistics
    QueryStats local_stats;
    if (!stats) stats = &local_stats;

    // Phase 1: Block Selection
    auto block_sel_start = std::chrono::high_resolution_clock::now();

    std::vector<BlockWithScore> blocks_with_score;

    // For each query term
    for (const auto& q_elem : query) {
        term_t term = q_elem.term;
        float q_weight = q_elem.score;

        if (term >= num_terms_) continue;

        // For each quantization block (use size_t to avoid uint8_t overflow at 256)
        for (size_t block_id = 0; block_id < config_.num_quantization_bins; ++block_id) {
            uint32_t block_size = block_sizes_[term][block_id];

            if (block_size > 0) {
                // Calculate block contribution (gain)
                float block_max_score = dequantizeScore(static_cast<uint8_t>(block_id));
                float gain = block_max_score * q_weight;

                blocks_with_score.emplace_back(
                    term, static_cast<uint8_t>(block_id), gain, &quantized_index_[term][block_id]
                );
            }
        }
    }

    stats->total_blocks = blocks_with_score.size();

    // Select blocks based on alpha parameter
    size_t selected_count = 0;
    if (params.alpha_mass) {
        selectBlocksAlphaMass(blocks_with_score, params.alpha, selected_count);
    } else {
        selectBlocksMaxRatio(blocks_with_score, params.alpha, selected_count);
    }

    stats->selected_blocks = selected_count;

    auto block_sel_end = std::chrono::high_resolution_clock::now();
    stats->block_selection_ms = std::chrono::duration<double, std::milli>(block_sel_end - block_sel_start).count();

    // Phase 2: ScatterAdd (Score Accumulation)
    auto scatter_start = std::chrono::high_resolution_clock::now();

    std::vector<int32_t> score_buf(config_.window_size, 0);
    std::vector<std::pair<int32_t, doc_id_t>> candidates;
    candidates.reserve(params.top_k_prime);

    scatterAdd(blocks_with_score, selected_count, score_buf, candidates, params.top_k_prime, stats);

    auto scatter_end = std::chrono::high_resolution_clock::now();
    stats->scatter_add_ms = std::chrono::duration<double, std::milli>(scatter_end - scatter_start).count();

    // Phase 3: Reranking
    auto rerank_start = std::chrono::high_resolution_clock::now();

    std::vector<doc_id_t> results;
    rerank(candidates, query, results, params.top_k, stats);

    auto rerank_end = std::chrono::high_resolution_clock::now();
    stats->reranking_ms = std::chrono::duration<double, std::milli>(rerank_end - rerank_start).count();

    auto end = std::chrono::high_resolution_clock::now();
    stats->total_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return results;
}

void BlockMaxQuantizedIndex::selectBlocksAlphaMass(std::vector<BlockWithScore>& blocks,
                                                    float alpha,
                                                    size_t& selected_count) {
    // Calculate total mass
    float total_mass = 0.0f;
    for (const auto& block : blocks) {
        total_mass += block.gain;
    }

    float target_mass = total_mass * alpha;

    // Sort by gain (descending)
    std::sort(blocks.begin(), blocks.end(),
              [](const BlockWithScore& a, const BlockWithScore& b) {
                  return a.gain > b.gain;
              });

    // Select blocks until we reach target mass
    float current_mass = 0.0f;
    selected_count = 0;

    for (size_t i = 0; i < blocks.size(); ++i) {
        current_mass += blocks[i].gain;
        selected_count++;

        if (current_mass >= target_mass) {
            break;
        }
    }
}

void BlockMaxQuantizedIndex::selectBlocksMaxRatio(std::vector<BlockWithScore>& blocks,
                                                   float alpha,
                                                   size_t& selected_count) {
    // Find max gain
    float max_gain = 0.0f;
    for (const auto& block : blocks) {
        max_gain = std::max(max_gain, block.gain);
    }

    float threshold = max_gain * alpha;

    // Partition blocks by threshold
    auto partition_point = std::partition(blocks.begin(), blocks.end(),
                                          [threshold](const BlockWithScore& block) {
                                              return block.gain >= threshold;
                                          });

    selected_count = std::distance(blocks.begin(), partition_point);

    // Sort selected blocks by gain
    std::sort(blocks.begin(), partition_point,
              [](const BlockWithScore& a, const BlockWithScore& b) {
                  return a.gain > b.gain;
              });
}

void BlockMaxQuantizedIndex::scatterAdd(const std::vector<BlockWithScore>& blocks,
                                       size_t selected_count,
                                       std::vector<int32_t>& score_buf,
                                       std::vector<std::pair<int32_t, doc_id_t>>& candidates,
                                       size_t top_k_prime,
                                       QueryStats* stats) {
    // Use a min-heap to maintain top-k' candidates
    auto cmp = [](const std::pair<int32_t, doc_id_t>& a, const std::pair<int32_t, doc_id_t>& b) {
        return a.first > b.first;  // Min-heap
    };

    // Process each window
    for (size_t window_id = 0; window_id < num_windows_; ++window_id) {
        doc_id_t window_offset = window_id * config_.window_size;

        // Accumulate scores for this window
        std::vector<doc_id_t> touched_docs;
        size_t max_window_docs = std::min(config_.window_size,
                                          static_cast<size_t>(num_documents_ - window_offset));
        touched_docs.reserve(std::min(max_window_docs, size_t(10000)));  // Reserve reasonable space

        for (size_t i = 0; i < selected_count; ++i) {
            const auto& block_entry = blocks[i];
            const auto& block = (*block_entry.blocks)[window_id];

            int32_t gain = static_cast<int32_t>(block_entry.gain * 1000.0f);  // Scale for precision

            for (doc_id_t local_doc_id : block.documents) {
                if (score_buf[local_doc_id] == 0) {
                    touched_docs.push_back(local_doc_id);
                }
                score_buf[local_doc_id] += gain;
                stats->score_operations++;
            }
        }

        // Extract candidates from this window and maintain top-k' heap
        for (doc_id_t local_doc_id : touched_docs) {
            int32_t score = score_buf[local_doc_id];
            doc_id_t global_doc_id = window_offset + local_doc_id;

            if (candidates.size() < top_k_prime) {
                candidates.emplace_back(score, global_doc_id);
                if (candidates.size() == top_k_prime) {
                    std::make_heap(candidates.begin(), candidates.end(), cmp);
                }
            } else if (score > candidates.front().first) {
                std::pop_heap(candidates.begin(), candidates.end(), cmp);
                candidates.back() = {score, global_doc_id};
                std::push_heap(candidates.begin(), candidates.end(), cmp);
            }

            // Reset score buffer
            score_buf[local_doc_id] = 0;
        }
    }

    // Sort candidates by score (descending)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.first > b.first;
              });
}

void BlockMaxQuantizedIndex::rerank(const std::vector<std::pair<int32_t, doc_id_t>>& candidates,
                                   const SparseDoc& query,
                                   std::vector<doc_id_t>& results,
                                   size_t top_k,
                                   QueryStats* stats) {
    // Exact scoring for candidates
    std::vector<std::pair<float, doc_id_t>> scored_candidates;
    scored_candidates.reserve(candidates.size());

    for (const auto& [approx_score, doc_id] : candidates) {
        if (doc_id >= forward_index_.size()) continue;

        float exact_score = dotProduct(query, forward_index_[doc_id]);
        scored_candidates.emplace_back(exact_score, doc_id);
    }

    // Sort by exact score
    std::sort(scored_candidates.begin(), scored_candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.first > b.first;
              });

    // Extract top-k
    results.clear();
    results.reserve(top_k);

    for (size_t i = 0; i < std::min(top_k, scored_candidates.size()); ++i) {
        results.push_back(scored_candidates[i].second);
    }
}

float BlockMaxQuantizedIndex::dotProduct(const SparseDoc& query, const SparseDoc& doc) const {
    // Use two-pointer approach for sorted sparse vectors
    // Assuming both query and doc are sorted by term
    float score = 0.0f;
    size_t q_idx = 0;
    size_t d_idx = 0;

    while (q_idx < query.size() && d_idx < doc.size()) {
        term_t q_term = query[q_idx].term;
        term_t d_term = doc[d_idx].term;

        if (q_term == d_term) {
            score += query[q_idx].score * doc[d_idx].score;
            q_idx++;
            d_idx++;
        } else if (q_term < d_term) {
            q_idx++;
        } else {
            d_idx++;
        }
    }

    return score;
}

size_t BlockMaxQuantizedIndex::memoryUsageBytes() const {
    size_t total = 0;

    // Quantized index
    for (const auto& term_blocks : quantized_index_) {
        for (const auto& blocks : term_blocks) {
            for (const auto& block : blocks) {
                total += block.documents.size() * sizeof(doc_id_t);
            }
        }
    }

    // Block sizes
    total += block_sizes_.size() * config_.num_quantization_bins * sizeof(uint32_t);

    // Forward index
    for (const auto& doc : forward_index_) {
        total += doc.size() * sizeof(SparseElement);
    }

    return total;
}

} // namespace index
} // namespace diagon
