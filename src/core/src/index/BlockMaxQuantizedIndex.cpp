// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/BlockMaxQuantizedIndex.h"
#include "diagon/index/TopKHolderOptimized.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace diagon {
namespace index {

BlockMaxQuantizedIndex::BlockMaxQuantizedIndex()
    : BlockMaxQuantizedIndex(Config()) {
}

BlockMaxQuantizedIndex::BlockMaxQuantizedIndex(const Config& config)
    : config_(config) {
    if (config_.use_custom_quantization) {
        // Load custom quantization from files
        std::cerr << "Using custom quantization:" << std::endl;
        std::cerr << "  LUT file: " << config_.lut_file << std::endl;
        std::cerr << "  Map file: " << config_.map_file << std::endl;
        loadCustomQuantization();
    } else {
        // Initialize default uniform quantization values
        quant_values_.resize(config_.num_quantization_bins);
        for (size_t i = 0; i < config_.num_quantization_bins; ++i) {
            quant_values_[i] = (static_cast<float>(i) / 255.0f) * config_.max_score;
        }
    }
}

BlockMaxQuantizedIndex::~BlockMaxQuantizedIndex() = default;

void BlockMaxQuantizedIndex::loadCustomQuantization() {
    // Load LUT file (N bin values)
    std::ifstream lut_file(config_.lut_file);
    if (!lut_file.is_open()) {
        throw std::runtime_error("Failed to open LUT file: " + config_.lut_file);
    }

    std::string line;
    std::getline(lut_file, line);
    std::stringstream ss(line);
    std::string token;

    quant_lut_.clear();
    while (std::getline(ss, token, ',')) {
        quant_lut_.push_back(std::stof(token));
    }

    if (quant_lut_.empty()) {
        throw std::runtime_error("LUT file is empty or invalid: " + config_.lut_file);
    }

    size_t actual_bins = quant_lut_.size();
    std::cerr << "Loaded custom quantization LUT with " << actual_bins << " bins" << std::endl;

    // Update num_quantization_bins to match LUT (const_cast needed)
    const_cast<size_t&>(config_.num_quantization_bins) = actual_bins;

    // Load mapping file (256 values -> N bins)
    std::ifstream map_file(config_.map_file);
    if (!map_file.is_open()) {
        throw std::runtime_error("Failed to open mapping file: " + config_.map_file);
    }

    std::getline(map_file, line);
    ss.clear();
    ss.str(line);

    quant_map_.clear();
    quant_map_.reserve(256);
    while (std::getline(ss, token, ',')) {
        quant_map_.push_back(static_cast<uint8_t>(std::stoi(token)));
    }

    if (quant_map_.size() != 256) {
        throw std::runtime_error("Mapping file must contain exactly 256 values, got " +
                                 std::to_string(quant_map_.size()));
    }

    // Validate mapping values are within range [0, actual_bins)
    for (size_t i = 0; i < quant_map_.size(); ++i) {
        if (quant_map_[i] >= actual_bins) {
            throw std::runtime_error("Invalid mapping: value " + std::to_string(quant_map_[i]) +
                                     " at index " + std::to_string(i) +
                                     " exceeds bin count " + std::to_string(actual_bins));
        }
    }

    std::cerr << "Loaded custom quantization mapping (256 -> " << actual_bins << " bins)" << std::endl;

    // Initialize quant_values_ for compatibility (use LUT values)
    quant_values_ = quant_lut_;
}

uint8_t BlockMaxQuantizedIndex::quantizeScore(float score) const {
    // Clamp to [0, max_score]
    score = std::max(0.0f, std::min(config_.max_score, score));

    // Map to [0, 255]
    uint8_t value_256 = static_cast<uint8_t>((score / config_.max_score) * 255.0f);

    if (config_.use_custom_quantization) {
        // Use custom mapping: 256 values -> N bins
        return quant_map_[value_256];
    } else {
        // Default: uniform quantization (value_256 is the bin)
        return value_256;
    }
}

float BlockMaxQuantizedIndex::dequantizeScore(uint8_t bin) const {
    if (config_.use_custom_quantization) {
        // Use custom LUT
        return quant_lut_[bin];
    } else {
        // Use default uniform quantization
        return quant_values_[bin];
    }
}

void BlockMaxQuantizedIndex::build(const std::vector<SparseDoc>& documents) {
    auto build_start = std::chrono::high_resolution_clock::now();

    num_documents_ = documents.size();
    num_windows_ = (num_documents_ + config_.window_size - 1) / config_.window_size;
    num_window_groups_ = (num_windows_ + config_.window_group_size - 1) / config_.window_group_size;

    std::cerr << "Building quantized index"
              << (config_.enable_on_demand_allocation ? " with on-demand allocation" : "")
              << std::endl;
    std::cerr << "Total docs: " << num_documents_
              << ", Window size: " << config_.window_size
              << ", Num windows: " << num_windows_
              << ", Window group size: " << config_.window_group_size
              << ", Num window groups: " << num_window_groups_ << std::endl;

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

    if (config_.enable_on_demand_allocation) {
        // ========== PASS 1: Determine max group_id for each term+block ==========
        std::cerr << "Pass 1: Determining group usage for on-demand allocation..." << std::endl;
        auto pass1_start = std::chrono::high_resolution_clock::now();

        // max_group_id[term][block] = highest group_id that has documents
        std::vector<std::vector<int>> max_group_id(num_terms_);
        for (size_t term = 0; term < num_terms_; ++term) {
            max_group_id[term].resize(config_.num_quantization_bins, -1);
        }

        // Scan all documents to find max group_id per term+block
        for (size_t doc_id = 0; doc_id < documents.size(); ++doc_id) {
            int window_id = doc_id / config_.window_size;
            int group_id = window_id / config_.window_group_size;

            const auto& doc = documents[doc_id];
            for (const auto& elem : doc) {
                term_t term = elem.term;
                float score = elem.score;

                // Quantize score to block
                uint8_t block_id = quantizeScore(score);

                // Update max group_id for this term+block
                if (block_id > 0) {  // Skip block 0 (very low scores)
                    max_group_id[term][block_id] = std::max(max_group_id[term][block_id], group_id);
                }
            }
        }

        auto pass1_end = std::chrono::high_resolution_clock::now();
        double pass1_time_ms = std::chrono::duration<double, std::milli>(pass1_end - pass1_start).count();
        std::cerr << "Pass 1 complete in " << pass1_time_ms << " ms" << std::endl;

        // ========== PASS 2: Allocate only needed groups ==========
        size_t total_possible_groups = num_terms_ * config_.num_quantization_bins * num_window_groups_;
        size_t allocated_groups = 0;
        size_t empty_term_blocks = 0;

        for (size_t term = 0; term < num_terms_; ++term) {
            quantized_index_[term].resize(config_.num_quantization_bins);
            block_sizes_[term].resize(config_.num_quantization_bins, 0);

            for (size_t block = 0; block < config_.num_quantization_bins; ++block) {
                int max_grp = max_group_id[term][block];
                if (max_grp >= 0) {
                    // Allocate only up to max_grp + 1 groups
                    quantized_index_[term][block].resize(max_grp + 1);
                    allocated_groups += (max_grp + 1);

                    // Initialize windows within each group
                    for (int g = 0; g <= max_grp; ++g) {
                        // Determine how many windows this group should have
                        int start_window = g * config_.window_group_size;
                        int end_window = std::min(start_window + (int)config_.window_group_size, (int)num_windows_);
                        int num_win_in_group = end_window - start_window;
                        quantized_index_[term][block][g].windows.resize(num_win_in_group);
                    }
                } else {
                    // Leave vector empty (no groups needed for this term+block)
                    empty_term_blocks++;
                }
            }
        }

        size_t skipped_groups = total_possible_groups - allocated_groups;
        double saved_percentage = 100.0 * skipped_groups / total_possible_groups;
        std::cerr << "Allocated " << allocated_groups << " groups, "
                  << "skipped " << skipped_groups << " empty groups ("
                  << std::fixed << std::setprecision(1) << saved_percentage << "% saved)" << std::endl;
        std::cerr << "Empty term+blocks: " << empty_term_blocks << std::endl;

    } else {
        // Traditional allocation: allocate all groups for all term+block combinations
        for (size_t term = 0; term < num_terms_; ++term) {
            quantized_index_[term].resize(config_.num_quantization_bins);
            block_sizes_[term].resize(config_.num_quantization_bins, 0);

            for (size_t block = 0; block < config_.num_quantization_bins; ++block) {
                quantized_index_[term][block].resize(num_window_groups_);

                // Initialize windows within each group
                for (size_t g = 0; g < num_window_groups_; ++g) {
                    int start_window = g * config_.window_group_size;
                    int end_window = std::min(start_window + (int)config_.window_group_size, (int)num_windows_);
                    int num_win_in_group = end_window - start_window;
                    quantized_index_[term][block][g].windows.resize(num_win_in_group);
                }
            }
        }
    }

    // Build inverted index
    for (size_t doc_id = 0; doc_id < documents.size(); ++doc_id) {
        int window_id = doc_id / config_.window_size;
        int group_id = window_id / config_.window_group_size;
        int sub_win = window_id % config_.window_group_size;
        doc_id_t local_doc_id = doc_id % config_.window_size;

        const auto& doc = documents[doc_id];
        for (const auto& elem : doc) {
            term_t term = elem.term;
            float score = elem.score;

            // Quantize score to block
            uint8_t block_id = quantizeScore(score);

            // On-demand allocation: check if group was allocated
            if (config_.enable_on_demand_allocation) {
                if (quantized_index_[term][block_id].empty() ||
                    group_id >= (int)quantized_index_[term][block_id].size()) {
                    continue;  // Group not allocated, skip
                }
            }

            // Verify sub_win is within allocated windows for this group
            if (sub_win >= (int)quantized_index_[term][block_id][group_id].windows.size()) {
                continue;  // Window not allocated within this group
            }

            // Add to inverted index
            quantized_index_[term][block_id][group_id].windows[sub_win].documents.push_back(local_doc_id);
            block_sizes_[term][block_id]++;
        }
    }

    // Store forward index for reranking
    forward_index_ = documents;

    auto build_end = std::chrono::high_resolution_clock::now();
    double build_time_ms = std::chrono::duration<double, std::milli>(build_end - build_start).count();
    std::cerr << "Index build complete in " << build_time_ms << " ms" << std::endl;
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
                // Calculate block contribution (gain) - use integer arithmetic like QBlock
                float block_max_score = dequantizeScore(static_cast<uint8_t>(block_id));
                uint32_t gain = static_cast<uint32_t>(block_max_score * q_weight);  // Integer gain
                float weight = static_cast<float>(gain);  // Float weight for mass accumulation

                blocks_with_score.emplace_back(
                    term, static_cast<uint8_t>(block_id), gain, weight, &quantized_index_[term][block_id]
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
    // Calculate total mass using weights (matches QBlock)
    float total_mass = 0.0f;
    for (const auto& block : blocks) {
        total_mass += block.weight;
    }

    float target_mass = total_mass * alpha;

    // Sort by gain (integer, for consistency with QBlock)
    std::sort(blocks.begin(), blocks.end(),
              [](const BlockWithScore& a, const BlockWithScore& b) {
                  return a.gain > b.gain;
              });

    // Select blocks until we reach target mass (using weights)
    float current_mass = 0.0f;
    selected_count = 0;

    for (size_t i = 0; i < blocks.size(); ++i) {
        current_mass += blocks[i].weight;
        selected_count++;

        if (current_mass >= target_mass) {
            break;
        }
    }
}

void BlockMaxQuantizedIndex::selectBlocksMaxRatio(std::vector<BlockWithScore>& blocks,
                                                   float alpha,
                                                   size_t& selected_count) {
    // Find max weight (matches QBlock)
    float max_weight = 0.0f;
    for (const auto& block : blocks) {
        max_weight = std::max(max_weight, block.weight);
    }

    float threshold = max_weight * alpha;

    // Partition blocks by threshold (using weight)
    auto partition_point = std::partition(blocks.begin(), blocks.end(),
                                          [threshold](const BlockWithScore& block) {
                                              return block.weight >= threshold;
                                          });

    selected_count = std::distance(blocks.begin(), partition_point);

    // Sort selected blocks by weight (matches QBlock)
    std::sort(blocks.begin(), partition_point,
              [](const BlockWithScore& a, const BlockWithScore& b) {
                  return a.weight > b.weight;
              });
}

void BlockMaxQuantizedIndex::scatterAdd(const std::vector<BlockWithScore>& blocks,
                                       size_t selected_count,
                                       std::vector<int32_t>& score_buf,
                                       std::vector<std::pair<int32_t, doc_id_t>>& candidates,
                                       size_t top_k_prime,
                                       QueryStats* stats) {
    // Use TopKHolderOptimized for efficient batch processing
    TopKHolderOptimized<doc_id_t, int32_t> topk_holder(top_k_prime);

    // Track timing for part1 (score accumulation) and part2 (TopK processing)
    double part1_time = 0.0;
    double part2_time = 0.0;

    // Pre-cache block metadata to reduce repeated lookups
    struct BlockCache {
        const std::vector<doc_id_t>* docs;
        int32_t gain;
    };
    std::vector<BlockCache> block_cache(selected_count);

    // Process each window
    for (size_t window_id = 0; window_id < num_windows_; ++window_id) {
        int group_id = window_id / config_.window_group_size;
        int sub_win = window_id % config_.window_group_size;
        doc_id_t window_offset = window_id * config_.window_size;

        // Part 1: Score accumulation (matching QBlock's design)
        auto part1_start = std::chrono::high_resolution_clock::now();

        // Cache posting lists for this window
        size_t valid_blocks = 0;
        for (size_t i = 0; i < selected_count; ++i) {
            const auto& block_entry = blocks[i];

            // On-demand allocation: check if group was allocated for this term+block
            if (config_.enable_on_demand_allocation) {
                if (block_entry.groups->empty() || group_id >= (int)block_entry.groups->size()) {
                    block_cache[i].docs = nullptr;
                    continue;
                }
            }

            const auto& group = (*block_entry.groups)[group_id];

            // Check if sub_win exists in this group
            if (sub_win >= (int)group.windows.size()) {
                block_cache[i].docs = nullptr;
                continue;
            }

            const auto& window = group.windows[sub_win];
            const auto& docs = window.documents;

            // Cache the posting list pointer and gain
            block_cache[i].docs = &docs;
            block_cache[i].gain = static_cast<int32_t>(block_entry.gain);

            // Prefetch the posting list data
            if (!docs.empty()) {
                __builtin_prefetch(&docs[0], 0, 1);
            }

            valid_blocks++;
        }

        // Part 1: Pure accumulation (no tracking overhead)
        int32_t* __restrict buf = score_buf.data();

        // Track documents from first block only (hint for Part 2)
        const std::vector<doc_id_t>* first_block_docs = nullptr;
        for (size_t i = 0; i < selected_count; ++i) {
            if (block_cache[i].docs != nullptr) {
                first_block_docs = block_cache[i].docs;
                break;
            }
        }

        for (size_t i = 0; i < selected_count; ++i) {
            if (block_cache[i].docs == nullptr) continue;

            const auto& docs = *block_cache[i].docs;
            const int32_t gain = block_cache[i].gain;
            const size_t n = docs.size();

            constexpr size_t kPrefetchDistance = 48;

            // Initial prefetch
            size_t pf_count = std::min(n, kPrefetchDistance);
            for (size_t p = 0; p < pf_count; ++p) {
                __builtin_prefetch(&buf[docs[p]], 1, 0);
            }

            // Main loop: pure accumulation (no tracking)
            size_t j = 0;
            for (; j + kPrefetchDistance < n; ++j) {
                __builtin_prefetch(&buf[docs[j + kPrefetchDistance]], 1, 0);
                buf[docs[j]] += gain;
            }

            // Tail loop
            for (; j < n; ++j) {
                buf[docs[j]] += gain;
            }

            stats->score_operations += n;
        }

        auto part1_end = std::chrono::high_resolution_clock::now();
        part1_time += std::chrono::duration<double, std::milli>(part1_end - part1_start).count();

        // Part 2: TopK processing using first block as hint
        auto part2_start = std::chrono::high_resolution_clock::now();

        // Use first block's posting list as a guide (covers most documents)
        if (first_block_docs != nullptr) {
            const auto& docs = *first_block_docs;
            const size_t n = docs.size();

            for (size_t j = 0; j < n; ++j) {
                doc_id_t local_doc_id = docs[j];
                int32_t score = buf[local_doc_id];

                if (score > 0) {
                    doc_id_t global_doc_id = window_offset + local_doc_id;
                    topk_holder.add(score, global_doc_id);
                    buf[local_doc_id] = 0;  // Reset for next window
                }
            }
        }

        // Scan remaining blocks for any missed documents (rare)
        for (size_t i = 1; i < selected_count; ++i) {
            if (block_cache[i].docs == nullptr || block_cache[i].docs == first_block_docs) continue;

            const auto& docs = *block_cache[i].docs;
            const size_t n = docs.size();

            for (size_t j = 0; j < n; ++j) {
                doc_id_t local_doc_id = docs[j];
                int32_t score = buf[local_doc_id];

                if (score > 0) {  // Not yet processed
                    doc_id_t global_doc_id = window_offset + local_doc_id;
                    topk_holder.add(score, global_doc_id);
                    buf[local_doc_id] = 0;
                }
            }
        }

        auto part2_end = std::chrono::high_resolution_clock::now();
        part2_time += std::chrono::duration<double, std::milli>(part2_end - part2_start).count();
    }

    // Store timing breakdown
    stats->scatter_add_part1_ms = part1_time;
    stats->scatter_add_part2_ms = part2_time;

    // Get top-k' candidates (already sorted by TopKHolderOptimized)
    auto [doc_ids, scores] = topk_holder.topKWithScores();
    candidates.clear();
    candidates.reserve(doc_ids.size());
    for (size_t i = 0; i < doc_ids.size(); ++i) {
        candidates.emplace_back(scores[i], doc_ids[i]);
    }
}

void BlockMaxQuantizedIndex::rerank(const std::vector<std::pair<int32_t, doc_id_t>>& candidates,
                                   const SparseDoc& query,
                                   std::vector<doc_id_t>& results,
                                   size_t top_k,
                                   QueryStats* stats) {
    // Use TopKHolderOptimized for exact scoring
    TopKHolderOptimized<doc_id_t, float> topk_holder(top_k);

    for (const auto& [approx_score, doc_id] : candidates) {
        if (doc_id >= forward_index_.size()) continue;

        float exact_score = dotProduct(query, forward_index_[doc_id]);
        topk_holder.add(exact_score, doc_id);
    }

    // Get top-k results (already sorted by TopKHolderOptimized)
    results = topk_holder.topK();
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

    // Quantized index: [term][block][group] -> WindowGroup
    for (const auto& term_blocks : quantized_index_) {
        for (const auto& blocks : term_blocks) {
            for (const auto& group : blocks) {
                // Each group contains multiple windows
                for (const auto& window : group.windows) {
                    total += window.documents.size() * sizeof(doc_id_t);
                }
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

const SparseDoc& BlockMaxQuantizedIndex::getDocument(doc_id_t doc_id) const {
    if (doc_id >= forward_index_.size()) {
        throw std::out_of_range("Document ID " + std::to_string(doc_id) +
                                " is out of range (max: " + std::to_string(forward_index_.size() - 1) + ")");
    }
    return forward_index_[doc_id];
}

std::vector<SparseDoc> BlockMaxQuantizedIndex::getDocuments(const std::vector<doc_id_t>& doc_ids) const {
    std::vector<SparseDoc> result;
    result.reserve(doc_ids.size());

    for (doc_id_t doc_id : doc_ids) {
        if (doc_id < forward_index_.size()) {
            result.push_back(forward_index_[doc_id]);
        } else {
            // Return empty document for invalid IDs (or could throw exception)
            result.push_back(SparseDoc());
        }
    }

    return result;
}

} // namespace index
} // namespace diagon
