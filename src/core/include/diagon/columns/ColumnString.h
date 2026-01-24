// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/columns/IColumn.h"
#include "diagon/columns/PODArray.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace diagon {
namespace columns {

/**
 * Column for variable-length strings.
 *
 * Storage: offsets array + chars buffer
 * - offsets[i] = end position of string i in chars
 * - string i = chars[offsets[i-1] .. offsets[i])
 *
 * Based on: ClickHouse ColumnString
 */
class ColumnString final : public IColumn {
public:
    using Chars = PODArray<uint8_t>;
    using Offsets = PODArray<uint64_t>;

    // ==================== Type ====================

    std::string getName() const override {
        return "String";
    }

    TypeIndex getDataType() const override {
        return TypeIndex::String;
    }

    // ==================== Size ====================

    size_t size() const override {
        return offsets_.size();
    }

    size_t byteSize() const override {
        return chars_.size() + offsets_.size() * sizeof(uint64_t);
    }

    // ==================== Data Access ====================

    Field operator[](size_t n) const override {
        size_t offset = offsetAt(n);
        size_t str_size = sizeAt(n);
        return Field(std::string(reinterpret_cast<const char*>(&chars_[offset]), str_size));
    }

    void get(size_t n, Field& res) const override {
        size_t offset = offsetAt(n);
        size_t str_size = sizeAt(n);
        res = Field(std::string(reinterpret_cast<const char*>(&chars_[offset]), str_size));
    }

    /**
     * Get string at index n as string_view
     */
    std::string_view getDataAt(size_t n) const {
        size_t offset = offsetAt(n);
        size_t str_size = sizeAt(n);
        return std::string_view(reinterpret_cast<const char*>(&chars_[offset]), str_size);
    }

    /**
     * Insert string data
     */
    void insertData(const char* pos, size_t length) {
        size_t old_size = chars_.size();
        chars_.resize(old_size + length);
        if (length > 0) {
            std::memcpy(&chars_[old_size], pos, length);
        }
        offsets_.push_back(chars_.size());
    }

    // ==================== Direct Access ====================

    Chars& getChars() {
        return chars_;
    }

    const Chars& getChars() const {
        return chars_;
    }

    Offsets& getOffsets() {
        return offsets_;
    }

    const Offsets& getOffsets() const {
        return offsets_;
    }

    // ==================== Insertion ====================

    void insert(const Field& x) override {
        const std::string& s = x.get<std::string>();
        insertData(s.data(), s.size());
    }

    void insertFrom(const IColumn& src, size_t n) override {
        const ColumnString& src_string = typedCast(src);
        std::string_view sv = src_string.getDataAt(n);
        insertData(sv.data(), sv.size());
    }

    void insertRangeFrom(const IColumn& src, size_t start, size_t length) override {
        const ColumnString& src_string = typedCast(src);

        if (length == 0) return;

        if (start + length > src_string.size()) {
            throw std::out_of_range("insertRangeFrom: range exceeds source size");
        }

        size_t nested_offset = src_string.offsetAt(start);
        size_t nested_length = src_string.offsets_[start + length - 1] - nested_offset;

        size_t old_chars_size = chars_.size();
        chars_.resize(old_chars_size + nested_length);
        if (nested_length > 0) {
            std::memcpy(&chars_[old_chars_size], &src_string.chars_[nested_offset], nested_length);
        }

        offsets_.reserve(offsets_.size() + length);
        size_t current_offset = old_chars_size;
        for (size_t i = 0; i < length; ++i) {
            current_offset += src_string.sizeAt(start + i);
            offsets_.push_back(current_offset);
        }
    }

    void insertDefault() override {
        insertData("", 0);
    }

    void popBack(size_t n) override {
        if (n > offsets_.size()) {
            throw std::out_of_range("popBack: n exceeds column size");
        }

        if (n == 0) return;

        size_t new_size = offsets_.size() - n;
        if (new_size > 0) {
            chars_.resize(offsets_[new_size - 1]);
        } else {
            chars_.clear();
        }
        offsets_.resize(new_size);
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

        auto res = ColumnString::create();
        res->getChars().reserve(result_size_hint * 10);  // Estimate: avg 10 bytes per string
        res->getOffsets().reserve(result_size_hint);

        for (size_t i = 0; i < count; ++i) {
            if (filt[i]) {
                res->insertFrom(*this, i);
            }
        }

        return res;
    }

    ColumnPtr cut(size_t offset, size_t length) const override {
        if (offset + length > size()) {
            throw std::out_of_range("cut: range exceeds column size");
        }

        auto res = ColumnString::create();
        if (length > 0) {
            res->insertRangeFrom(*this, offset, length);
        }
        return res;
    }

    // ==================== Comparison ====================

    int compareAt(size_t n, size_t m, const IColumn& rhs,
                 int /*nan_direction_hint*/) const override {
        const ColumnString& rhs_string = typedCast(rhs);

        std::string_view lhs_sv = getDataAt(n);
        std::string_view rhs_sv = rhs_string.getDataAt(m);

        return lhs_sv.compare(rhs_sv);
    }

    // ==================== Cloning ====================

    MutableColumnPtr clone() const override {
        auto res = ColumnString::create();
        res->chars_ = chars_;
        res->offsets_ = offsets_;
        return res;
    }

    MutableColumnPtr cloneResized(size_t new_size) const override {
        auto res = ColumnString::create();
        if (new_size > 0) {
            size_t count = std::min(this->size(), new_size);
            if (count > 0) {
                res->insertRangeFrom(*this, 0, count);
            }
            // Fill remaining with defaults
            for (size_t i = count; i < new_size; ++i) {
                res->insertDefault();
            }
        }
        return res;
    }

    MutableColumnPtr cloneEmpty() const override {
        return ColumnString::create();
    }

    // ==================== Factory ====================

    static std::shared_ptr<ColumnString> create() {
        return std::make_shared<ColumnString>();
    }

private:
    uint64_t offsetAt(size_t i) const {
        return i == 0 ? 0 : offsets_[i - 1];
    }

    size_t sizeAt(size_t i) const {
        return i == 0 ? offsets_[0] : (offsets_[i] - offsets_[i - 1]);
    }

    // Type-safe cast helper
    static const ColumnString& typedCast(const IColumn& col) {
        const ColumnString* ptr = dynamic_cast<const ColumnString*>(&col);
        if (!ptr) {
            throw std::bad_cast();
        }
        return *ptr;
    }

    Chars chars_;      // Concatenated string data
    Offsets offsets_;  // End positions
};

}  // namespace columns
}  // namespace diagon
