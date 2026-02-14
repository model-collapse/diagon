// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/SparseVector.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace diagon {
namespace sparse {

// ==================== Construction ====================

SparseVector::SparseVector(const std::vector<uint32_t>& indices, const std::vector<float>& values) {
    if (indices.size() != values.size()) {
        throw std::invalid_argument("indices and values must have same size");
    }

    elements_.reserve(indices.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        if (values[i] != 0.0f) {  // Skip zeros
            elements_.emplace_back(indices[i], values[i]);
        }
    }

    sortByIndex();
}

SparseVector::SparseVector(const std::vector<SparseElement>& elements)
    : elements_(elements) {
    sortByIndex();
}

// ==================== Modification ====================

void SparseVector::add(uint32_t index, float value) {
    if (value == 0.0f)
        return;

    auto it = findElement(index);
    if (it != elements_.end()) {
        // Index exists, add to existing value
        it->value += value;
        if (it->value == 0.0f) {
            // Remove if sum is zero
            elements_.erase(it);
        }
    } else {
        // Insert new element (maintain sorted order)
        it = std::lower_bound(elements_.begin(), elements_.end(), SparseElement(index, 0.0f));
        elements_.insert(it, SparseElement(index, value));
    }
}

void SparseVector::set(uint32_t index, float value) {
    if (value == 0.0f) {
        // Remove element if setting to zero
        auto it = findElement(index);
        if (it != elements_.end()) {
            elements_.erase(it);
        }
        return;
    }

    auto it = findElement(index);
    if (it != elements_.end()) {
        // Update existing value
        it->value = value;
    } else {
        // Insert new element (maintain sorted order)
        it = std::lower_bound(elements_.begin(), elements_.end(), SparseElement(index, 0.0f));
        elements_.insert(it, SparseElement(index, value));
    }
}

// ==================== Access ====================

float SparseVector::get(uint32_t index) const {
    auto it = findElement(index);
    return (it != elements_.end()) ? it->value : 0.0f;
}

bool SparseVector::contains(uint32_t index) const {
    return findElement(index) != elements_.end();
}

uint32_t SparseVector::maxDimension() const {
    if (elements_.empty())
        return 0;
    return elements_.back().index + 1;
}

// ==================== Vector Operations ====================

float SparseVector::dot(const SparseVector& other) const {
    float result = 0.0f;

    // Two-pointer algorithm (both vectors sorted by index)
    size_t i = 0, j = 0;
    while (i < elements_.size() && j < other.elements_.size()) {
        if (elements_[i].index == other.elements_[j].index) {
            result += elements_[i].value * other.elements_[j].value;
            ++i;
            ++j;
        } else if (elements_[i].index < other.elements_[j].index) {
            ++i;
        } else {
            ++j;
        }
    }

    return result;
}

float SparseVector::norm() const {
    float sum_sq = 0.0f;
    for (const auto& elem : elements_) {
        sum_sq += elem.value * elem.value;
    }
    return std::sqrt(sum_sq);
}

float SparseVector::norm1() const {
    float sum = 0.0f;
    for (const auto& elem : elements_) {
        sum += std::abs(elem.value);
    }
    return sum;
}

float SparseVector::sum() const {
    float total = 0.0f;
    for (const auto& elem : elements_) {
        total += elem.value;
    }
    return total;
}

float SparseVector::cosineSimilarity(const SparseVector& other) const {
    float dot_product = dot(other);
    float norm_product = norm() * other.norm();

    if (norm_product == 0.0f)
        return 0.0f;
    return dot_product / norm_product;
}

// ==================== Pruning ====================

void SparseVector::pruneTopK(size_t k, bool by_value) {
    if (elements_.size() <= k)
        return;

    if (by_value) {
        // Sort by absolute value (descending)
        std::partial_sort(elements_.begin(), elements_.begin() + k, elements_.end(),
                          [](const SparseElement& a, const SparseElement& b) {
                              return std::abs(a.value) > std::abs(b.value);
                          });
    }

    // Keep only top k
    elements_.resize(k);

    // Re-sort by index
    sortByIndex();
}

void SparseVector::pruneByMass(float alpha) {
    if (alpha >= 1.0f || elements_.empty())
        return;

    // Sort by absolute value (descending)
    std::vector<SparseElement> sorted = elements_;
    std::sort(sorted.begin(), sorted.end(), [](const SparseElement& a, const SparseElement& b) {
        return std::abs(a.value) > std::abs(b.value);
    });

    // Calculate total mass
    float total_mass = 0.0f;
    for (const auto& elem : sorted) {
        total_mass += std::abs(elem.value);
    }

    float target_mass = alpha * total_mass;

    // Find threshold
    float current_mass = 0.0f;
    float threshold = 0.0f;
    for (const auto& elem : sorted) {
        current_mass += std::abs(elem.value);
        threshold = std::abs(elem.value);
        if (current_mass >= target_mass)
            break;
    }

    // Filter by threshold
    pruneByThreshold(threshold);
}

void SparseVector::pruneByThreshold(float threshold) {
    elements_.erase(std::remove_if(elements_.begin(), elements_.end(),
                                   [threshold](const SparseElement& elem) {
                                       return std::abs(elem.value) < threshold;
                                   }),
                    elements_.end());
}

// ==================== Normalization ====================

void SparseVector::normalize() {
    float n = norm();
    if (n == 0.0f)
        return;

    scale(1.0f / n);
}

void SparseVector::scale(float factor) {
    for (auto& elem : elements_) {
        elem.value *= factor;
    }
}

// ==================== Sorting ====================

void SparseVector::sortByIndex() {
    std::sort(elements_.begin(), elements_.end(),
              [](const SparseElement& a, const SparseElement& b) { return a.index < b.index; });
}

void SparseVector::sortByValue() {
    std::sort(elements_.begin(), elements_.end(),
              [](const SparseElement& a, const SparseElement& b) {
                  return std::abs(a.value) > std::abs(b.value);
              });
}

// ==================== Conversion ====================

std::vector<float> SparseVector::toDense(uint32_t dimension) const {
    if (dimension == 0) {
        dimension = maxDimension();
    }

    std::vector<float> dense(dimension, 0.0f);
    for (const auto& elem : elements_) {
        if (elem.index < dimension) {
            dense[elem.index] = elem.value;
        }
    }

    return dense;
}

SparseVector SparseVector::fromDense(const std::vector<float>& dense, float threshold) {
    SparseVector result;
    result.reserve(dense.size() / 10);  // Estimate 10% sparsity

    for (uint32_t i = 0; i < dense.size(); ++i) {
        if (std::abs(dense[i]) > threshold) {
            result.elements_.emplace_back(i, dense[i]);
        }
    }

    return result;
}

// ==================== Private Methods ====================

std::vector<SparseElement>::iterator SparseVector::findElement(uint32_t index) {
    auto it = std::lower_bound(elements_.begin(), elements_.end(), SparseElement(index, 0.0f));
    if (it != elements_.end() && it->index == index) {
        return it;
    }
    return elements_.end();
}

std::vector<SparseElement>::const_iterator SparseVector::findElement(uint32_t index) const {
    auto it = std::lower_bound(elements_.begin(), elements_.end(), SparseElement(index, 0.0f));
    if (it != elements_.end() && it->index == index) {
        return it;
    }
    return elements_.end();
}

}  // namespace sparse
}  // namespace diagon
