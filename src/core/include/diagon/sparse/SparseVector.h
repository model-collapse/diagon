// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace diagon {
namespace sparse {

/**
 * Sparse vector element: (index, value) pair
 *
 * Represents a single non-zero entry in a sparse vector.
 * Elements are typically stored sorted by index for efficient operations.
 */
struct SparseElement {
    uint32_t index;  // Dimension index
    float value;     // Weight/score

    SparseElement()
        : index(0)
        , value(0.0f) {}
    SparseElement(uint32_t idx, float val)
        : index(idx)
        , value(val) {}

    bool operator<(const SparseElement& other) const { return index < other.index; }

    bool operator==(const SparseElement& other) const {
        return index == other.index && value == other.value;
    }
};

/**
 * Sparse vector: efficient representation of high-dimensional vectors
 *
 * Stores only non-zero elements as sorted (index, value) pairs.
 * Used for learned sparse retrieval (SPLADE), BM25 expansions, etc.
 *
 * Example:
 * ```cpp
 * SparseVector vec;
 * vec.add(10, 0.8f);
 * vec.add(25, 1.2f);
 * vec.add(100, 0.5f);
 *
 * float score = vec.dot(query_vec);
 * vec.pruneByMass(0.9f);  // Keep 90% of mass
 * ```
 *
 * Based on: BitQ SparseVector and ClickHouse column storage patterns
 */
class SparseVector {
public:
    SparseVector() = default;

    /**
     * Construct from arrays (common input format)
     */
    SparseVector(const std::vector<uint32_t>& indices, const std::vector<float>& values);

    /**
     * Construct from element list
     */
    explicit SparseVector(const std::vector<SparseElement>& elements);

    // ==================== Modification ====================

    /**
     * Add element (maintains sorted order)
     *
     * If index already exists, adds to existing value.
     * Complexity: O(n) worst case (insertion)
     */
    void add(uint32_t index, float value);

    /**
     * Set element (replaces existing value)
     *
     * If index doesn't exist, adds new element.
     * Complexity: O(n) worst case
     */
    void set(uint32_t index, float value);

    /**
     * Reserve capacity
     */
    void reserve(size_t capacity) { elements_.reserve(capacity); }

    /**
     * Clear all elements
     */
    void clear() { elements_.clear(); }

    // ==================== Access ====================

    /**
     * Number of non-zero elements
     */
    size_t size() const { return elements_.size(); }

    /**
     * Check if vector is empty
     */
    bool empty() const { return elements_.empty(); }

    /**
     * Access element by position
     */
    const SparseElement& operator[](size_t i) const { return elements_[i]; }

    /**
     * Get value at index (returns 0.0 if not present)
     */
    float get(uint32_t index) const;

    /**
     * Check if index exists
     */
    bool contains(uint32_t index) const;

    /**
     * Get maximum dimension index + 1
     */
    uint32_t maxDimension() const;

    // ==================== Iteration ====================

    auto begin() const { return elements_.begin(); }
    auto end() const { return elements_.end(); }
    auto begin() { return elements_.begin(); }
    auto end() { return elements_.end(); }

    const std::vector<SparseElement>& elements() const { return elements_; }

    // ==================== Vector Operations ====================

    /**
     * Dot product with another sparse vector
     *
     * Complexity: O(n + m) where n, m are sizes
     * Uses two-pointer algorithm for sorted elements
     */
    float dot(const SparseVector& other) const;

    /**
     * L2 norm (Euclidean length)
     */
    float norm() const;

    /**
     * L1 norm (sum of absolute values)
     */
    float norm1() const;

    /**
     * Sum of all values
     */
    float sum() const;

    /**
     * Cosine similarity with another vector
     * Returns dot(this, other) / (norm(this) * norm(other))
     */
    float cosineSimilarity(const SparseVector& other) const;

    // ==================== Pruning ====================

    /**
     * Keep only top-k elements by absolute value
     *
     * Reduces vector size to at most k elements.
     * Useful for query optimization.
     *
     * @param k Maximum number of elements to keep
     * @param by_value If true, sort by value; if false, keep largest indices
     */
    void pruneTopK(size_t k, bool by_value = true);

    /**
     * Alpha-mass pruning: keep elements covering alpha% of total mass
     *
     * Sorts elements by value (descending) and keeps elements until
     * their cumulative sum reaches alpha * total_sum.
     *
     * Example: alpha=0.9 keeps smallest set covering 90% of total weight.
     *
     * @param alpha Fraction of mass to keep (0.0-1.0)
     */
    void pruneByMass(float alpha);

    /**
     * Remove elements with value below threshold
     */
    void pruneByThreshold(float threshold);

    // ==================== Normalization ====================

    /**
     * L2 normalize (make norm() = 1.0)
     */
    void normalize();

    /**
     * Scale all values by factor
     */
    void scale(float factor);

    // ==================== Sorting ====================

    /**
     * Sort elements by index (ascending)
     * Usually already sorted, but useful after bulk operations
     */
    void sortByIndex();

    /**
     * Sort elements by value (descending)
     * Used before mass-based pruning
     */
    void sortByValue();

    // ==================== Conversion ====================

    /**
     * Convert to dense vector (fills zeros)
     *
     * @param dimension Target dimension (0 = auto-detect from max index)
     */
    std::vector<float> toDense(uint32_t dimension = 0) const;

    /**
     * Create from dense vector (extracts non-zeros)
     *
     * @param dense Dense vector
     * @param threshold Minimum absolute value to include (default: >0)
     */
    static SparseVector fromDense(const std::vector<float>& dense, float threshold = 0.0f);

private:
    std::vector<SparseElement> elements_;  // Sorted by index

    // Find element by index (binary search)
    // Returns iterator to element or end() if not found
    std::vector<SparseElement>::iterator findElement(uint32_t index);
    std::vector<SparseElement>::const_iterator findElement(uint32_t index) const;
};

}  // namespace sparse
}  // namespace diagon
