// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0
//
// Based on QBlock's topk_optimized.h
// Optimized top-K selection using batch processing with nth_element.

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace diagon {
namespace index {

/**
 * Efficient top-K holder using batched partial sorting.
 *
 * Key optimizations:
 * - Lazy candidate accumulation (avoids repeated sorting)
 * - Batched nth_element (3K buffer before partial sort)
 * - Threshold-based pruning (quick rejection of low scores)
 *
 * Much faster than:
 * - std::priority_queue: O(log K) per insertion
 * - std::sort: O(N log N) per batch
 * - nth_element: O(N) amortized over batches
 *
 * Based on: QBlock's TopKHolderOptimized
 * Reference: /home/ubuntu/bitq-code/QBlock/QBlock/src/topk_optimized.h
 */
template <typename T, typename V = float>
class TopKHolderOptimized {
public:
    /**
     * Constructor
     * @param k Number of top items to maintain
     */
    explicit TopKHolderOptimized(int k) : k_(k), threshold_(0) {
        candidates_.reserve(k * 3);
    }

    /**
     * Add item with threshold check and batch processing
     * @param score Score of the item
     * @param item Item identifier
     */
    void add(V score, const T& item) {
        if (score > threshold_) {
            candidates_.emplace_back(score, item);
            if (candidates_.size() >= static_cast<size_t>(k_ * 3)) {
                fitToK();
            }
        }
    }

    /**
     * Add item without any checks (for initial population)
     * @param score Score of the item
     * @param item Item identifier
     */
    void addSimple(V score, const T& item) {
        candidates_.emplace_back(score, item);
    }

    /**
     * Add item with threshold comparison only (no auto-fitting)
     * @param score Score of the item
     * @param item Item identifier
     */
    void addWithCompare(V score, const T& item) {
        if (score > threshold_) {
            candidates_.emplace_back(score, item);
        }
    }

    /**
     * Reduce to K elements if significantly over capacity
     */
    void fitToK() {
        // Only fit if we're significantly over capacity (7K/4 = 1.75K)
        if (candidates_.size() <= static_cast<size_t>(7 * k_ / 4)) {
            return;
        }
        processBatch();
    }

    /**
     * Return top K items (destructive operation)
     * @return Vector of top K item identifiers
     */
    std::vector<T> topK() {
        if (!candidates_.empty()) {
            processBatch();
        }
        std::vector<T> result;
        result.reserve(candidates_.size());
        for (const auto& pair : candidates_) {
            result.push_back(pair.second);
        }
        return result;
    }

    /**
     * Return top K items with their scores
     * @return Pair of (items, scores)
     */
    std::pair<std::vector<T>, std::vector<V>> topKWithScores() {
        if (!candidates_.empty()) {
            processBatch();
        }
        std::vector<T> ids;
        std::vector<V> scores;
        ids.reserve(candidates_.size());
        scores.reserve(candidates_.size());

        for (const auto& pair : candidates_) {
            scores.push_back(pair.first);
            ids.push_back(pair.second);
        }
        return {ids, scores};
    }

    /**
     * Get current number of candidates
     */
    size_t size() const { return candidates_.size(); }

    /**
     * Clear all candidates
     */
    void clear() {
        candidates_.clear();
        threshold_ = 0;
    }

private:
    /**
     * Process batch: partial sort to keep top K
     */
    void processBatch() {
        if (candidates_.size() <= static_cast<size_t>(k_)) {
            return;
        }

        // Partial sort: top k elements using nth_element
        // This is O(N) average case, much faster than full sort O(N log N)
        std::nth_element(
            candidates_.begin(),
            candidates_.begin() + k_,
            candidates_.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // Keep only top k and update threshold
        candidates_.resize(k_);
        threshold_ = candidates_.back().first;
    }

    std::vector<std::pair<V, T>> candidates_;  // (score, item) pairs
    int k_;                                     // Target number of items
    V threshold_;                               // Minimum score for inclusion
};

}  // namespace index
}  // namespace diagon
