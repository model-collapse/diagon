// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace diagon {
namespace columns {

/**
 * PODArray is an optimized vector for POD (Plain Old Data) types.
 *
 * Optimizations over std::vector:
 * - memcpy for bulk operations
 * - No destructor calls (POD)
 * - Custom growth strategy
 *
 * Based on: ClickHouse PODArray
 *
 * NOTE: Simplified implementation - uses std::vector internally for now.
 * Future: Implement custom allocator with POD optimizations.
 */
template<typename T>
class PODArray {
public:
    using value_type = T;
    using size_type = size_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    // ==================== Construction ====================

    PODArray() = default;

    explicit PODArray(size_t count)
        : data_(count) {}

    PODArray(size_t count, const T& value)
        : data_(count, value) {}

    // ==================== Size ====================

    size_t size() const { return data_.size(); }

    size_t capacity() const { return data_.capacity(); }

    bool empty() const { return data_.empty(); }

    void resize(size_t new_size) { data_.resize(new_size); }

    void resize(size_t new_size, const T& value) { data_.resize(new_size, value); }

    void reserve(size_t new_capacity) { data_.reserve(new_capacity); }

    void clear() { data_.clear(); }

    // ==================== Element Access ====================

    T& operator[](size_t index) { return data_[index]; }

    const T& operator[](size_t index) const { return data_[index]; }

    T& at(size_t index) {
        if (index >= size()) {
            throw std::out_of_range("PODArray index out of range");
        }
        return data_[index];
    }

    const T& at(size_t index) const {
        if (index >= size()) {
            throw std::out_of_range("PODArray index out of range");
        }
        return data_[index];
    }

    T& front() { return data_.front(); }

    const T& front() const { return data_.front(); }

    T& back() { return data_.back(); }

    const T& back() const { return data_.back(); }

    T* data() { return data_.data(); }

    const T* data() const { return data_.data(); }

    // ==================== Modifiers ====================

    void push_back(const T& value) { data_.push_back(value); }

    void push_back(T&& value) { data_.push_back(std::move(value)); }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        data_.emplace_back(std::forward<Args>(args)...);
    }

    void pop_back() { data_.pop_back(); }

    // ==================== Iterators ====================

    iterator begin() { return data_.data(); }

    const_iterator begin() const { return data_.data(); }

    const_iterator cbegin() const { return data_.data(); }

    iterator end() { return data_.data() + data_.size(); }

    const_iterator end() const { return data_.data() + data_.size(); }

    const_iterator cend() const { return data_.data() + data_.size(); }

private:
    std::vector<T> data_;
};

}  // namespace columns
}  // namespace diagon
