// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/columns/IColumn.h"
#include "diagon/columns/PODArray.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace diagon {
namespace columns {

/**
 * Type name helper
 */
template <typename T>
struct TypeName;

template <> struct TypeName<uint8_t> { static const char* get() { return "UInt8"; } };
template <> struct TypeName<uint16_t> { static const char* get() { return "UInt16"; } };
template <> struct TypeName<uint32_t> { static const char* get() { return "UInt32"; } };
template <> struct TypeName<uint64_t> { static const char* get() { return "UInt64"; } };
template <> struct TypeName<int8_t> { static const char* get() { return "Int8"; } };
template <> struct TypeName<int16_t> { static const char* get() { return "Int16"; } };
template <> struct TypeName<int32_t> { static const char* get() { return "Int32"; } };
template <> struct TypeName<int64_t> { static const char* get() { return "Int64"; } };
template <> struct TypeName<float> { static const char* get() { return "Float32"; } };
template <> struct TypeName<double> { static const char* get() { return "Float64"; } };

/**
 * Type index helper
 */
template <typename T>
struct TypeIndexForType;

template <> struct TypeIndexForType<uint8_t> { static constexpr TypeIndex value = TypeIndex::UInt8; };
template <> struct TypeIndexForType<uint16_t> { static constexpr TypeIndex value = TypeIndex::UInt16; };
template <> struct TypeIndexForType<uint32_t> { static constexpr TypeIndex value = TypeIndex::UInt32; };
template <> struct TypeIndexForType<uint64_t> { static constexpr TypeIndex value = TypeIndex::UInt64; };
template <> struct TypeIndexForType<int8_t> { static constexpr TypeIndex value = TypeIndex::Int8; };
template <> struct TypeIndexForType<int16_t> { static constexpr TypeIndex value = TypeIndex::Int16; };
template <> struct TypeIndexForType<int32_t> { static constexpr TypeIndex value = TypeIndex::Int32; };
template <> struct TypeIndexForType<int64_t> { static constexpr TypeIndex value = TypeIndex::Int64; };
template <> struct TypeIndexForType<float> { static constexpr TypeIndex value = TypeIndex::Float32; };
template <> struct TypeIndexForType<double> { static constexpr TypeIndex value = TypeIndex::Float64; };

/**
 * Column for fixed-size numeric types.
 * Uses PODArray<T> for efficient storage and operations.
 *
 * Based on: ClickHouse ColumnVector
 */
template <typename T>
class ColumnVector final : public IColumn {
public:
    using Self = ColumnVector<T>;
    using value_type = T;
    using Container = PODArray<T>;

    static_assert(std::is_arithmetic_v<T>, "ColumnVector only supports arithmetic types");

    // ==================== Construction ====================

    ColumnVector() = default;

    explicit ColumnVector(size_t n) : data_(n) {}

    ColumnVector(size_t n, const T& value) : data_(n, value) {}

    // ==================== Type ====================

    std::string getName() const override {
        return TypeName<T>::get();
    }

    TypeIndex getDataType() const override {
        return TypeIndexForType<T>::value;
    }

    // ==================== Size ====================

    size_t size() const override {
        return data_.size();
    }

    size_t byteSize() const override {
        return data_.size() * sizeof(T);
    }

    // ==================== Data Access ====================

    Field operator[](size_t n) const override {
        return Field(data_[n]);
    }

    void get(size_t n, Field& res) const override {
        res = Field(data_[n]);
    }

    /**
     * Direct access to element
     */
    T& getElement(size_t n) {
        return data_[n];
    }

    const T& getElement(size_t n) const {
        return data_[n];
    }

    /**
     * Direct access to data container
     */
    Container& getData() {
        return data_;
    }

    const Container& getData() const {
        return data_;
    }

    const char* getRawData() const override {
        return reinterpret_cast<const char*>(data_.data());
    }

    bool isNumeric() const override {
        return true;
    }

    // ==================== Insertion ====================

    void insert(const Field& x) override {
        if constexpr (std::is_same_v<T, uint64_t>) {
            data_.push_back(x.get<uint64_t>());
        } else if constexpr (std::is_same_v<T, int64_t>) {
            data_.push_back(x.get<int64_t>());
        } else if constexpr (std::is_same_v<T, float>) {
            data_.push_back(x.get<float>());
        } else if constexpr (std::is_same_v<T, double>) {
            data_.push_back(x.get<double>());
        } else {
            // For smaller integer types, extract from int64_t/uint64_t
            if constexpr (std::is_signed_v<T>) {
                data_.push_back(static_cast<T>(x.get<int64_t>()));
            } else {
                data_.push_back(static_cast<T>(x.get<uint64_t>()));
            }
        }
    }

    void insertFrom(const IColumn& src, size_t n) override {
        const Self& src_vec = typedCast(src);
        data_.push_back(src_vec.getData()[n]);
    }

    void insertRangeFrom(const IColumn& src, size_t start, size_t length) override {
        const Self& src_vec = typedCast(src);
        const auto& src_data = src_vec.getData();

        if (start + length > src_data.size()) {
            throw std::out_of_range("insertRangeFrom: range exceeds source size");
        }

        size_t old_size = data_.size();
        data_.resize(old_size + length);
        std::memcpy(&data_[old_size], &src_data[start], length * sizeof(T));
    }

    void insertDefault() override {
        data_.push_back(T{});
    }

    void insertManyDefaults(size_t length) override {
        size_t old_size = data_.size();
        data_.resize(old_size + length);
        std::memset(&data_[old_size], 0, length * sizeof(T));
    }

    void popBack(size_t n) override {
        if (n > data_.size()) {
            throw std::out_of_range("popBack: n exceeds column size");
        }
        data_.resize(data_.size() - n);
    }

    // ==================== Filtering ====================

    ColumnPtr filter(const Filter& filt, ssize_t result_size_hint) const override {
        size_t count = filt.size();
        if (count != size()) {
            throw std::runtime_error("Size of filter doesn't match column size");
        }

        if (result_size_hint < 0) {
            result_size_hint = static_cast<ssize_t>(countBytesInFilter(filt));
        }

        auto res = Self::create();
        res->getData().reserve(result_size_hint);

        for (size_t i = 0; i < count; ++i) {
            if (filt[i]) {
                res->getData().push_back(data_[i]);
            }
        }

        return res;
    }

    ColumnPtr cut(size_t offset, size_t length) const override {
        if (offset + length > size()) {
            throw std::out_of_range("cut: range exceeds column size");
        }

        auto res = Self::create();
        res->getData().resize(length);
        std::memcpy(res->getData().data(), &data_[offset], length * sizeof(T));
        return res;
    }

    // ==================== Comparison ====================

    int compareAt(size_t n, size_t m, const IColumn& rhs,
                 int nan_direction_hint) const override {
        const Self& rhs_vec = typedCast(rhs);

        if constexpr (std::is_floating_point_v<T>) {
            // Handle NaN
            bool lhs_is_nan = std::isnan(data_[n]);
            bool rhs_is_nan = std::isnan(rhs_vec.data_[m]);

            if (lhs_is_nan || rhs_is_nan) {
                if (lhs_is_nan && rhs_is_nan) return 0;
                return lhs_is_nan ? nan_direction_hint : -nan_direction_hint;
            }
        }

        if (data_[n] < rhs_vec.data_[m]) return -1;
        if (data_[n] > rhs_vec.data_[m]) return 1;
        return 0;
    }

    // ==================== Cloning ====================

    MutableColumnPtr clone() const override {
        auto res = Self::create(data_.size());
        res->getData() = data_;
        return res;
    }

    MutableColumnPtr cloneResized(size_t new_size) const override {
        auto res = Self::create();
        if (new_size > 0) {
            size_t count = std::min(this->size(), new_size);
            res->getData().resize(new_size);
            if (count > 0) {
                std::memcpy(res->getData().data(), data_.data(), count * sizeof(T));
            }
            // Rest is zero-initialized
            if (new_size > count) {
                std::memset(res->getData().data() + count, 0, (new_size - count) * sizeof(T));
            }
        }
        return res;
    }

    MutableColumnPtr cloneEmpty() const override {
        return Self::create();
    }

    // ==================== Factory ====================

    static std::shared_ptr<Self> create() {
        return std::make_shared<Self>();
    }

    static std::shared_ptr<Self> create(size_t n) {
        return std::make_shared<Self>(n);
    }

    static std::shared_ptr<Self> create(size_t n, const T& value) {
        return std::make_shared<Self>(n, value);
    }

private:
    Container data_;

    // Type-safe cast helper
    static const Self& typedCast(const IColumn& col) {
        const Self* ptr = dynamic_cast<const Self*>(&col);
        if (!ptr) {
            throw std::bad_cast();
        }
        return *ptr;
    }
};

// Common numeric column types
using ColumnUInt8 = ColumnVector<uint8_t>;
using ColumnUInt16 = ColumnVector<uint16_t>;
using ColumnUInt32 = ColumnVector<uint32_t>;
using ColumnUInt64 = ColumnVector<uint64_t>;
using ColumnInt8 = ColumnVector<int8_t>;
using ColumnInt16 = ColumnVector<int16_t>;
using ColumnInt32 = ColumnVector<int32_t>;
using ColumnInt64 = ColumnVector<int64_t>;
using ColumnFloat32 = ColumnVector<float>;
using ColumnFloat64 = ColumnVector<double>;

}  // namespace columns
}  // namespace diagon
