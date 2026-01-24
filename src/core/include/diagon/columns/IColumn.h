// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/columns/Field.h"
#include "diagon/columns/TypeIndex.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace columns {

// Forward declarations
class IColumn;

using ColumnPtr = std::shared_ptr<const IColumn>;
using MutableColumnPtr = std::shared_ptr<IColumn>;
using Columns = std::vector<ColumnPtr>;
using MutableColumns = std::vector<MutableColumnPtr>;

// Filter type (bit mask for filtering rows)
using Filter = std::vector<uint8_t>;

// Permutation type (for reordering rows)
using Permutation = std::vector<size_t>;

/**
 * IColumn is the in-memory representation of a column.
 *
 * COW (Copy-On-Write) semantics:
 * - Columns are immutable by default
 * - mutate() creates writable copy if shared
 * - Efficient for caching and sharing
 *
 * Based on: ClickHouse IColumn
 *
 * NOTE: Simplified implementation focusing on core operations.
 * Full ClickHouse IColumn has many more specialized operations.
 */
class IColumn : public std::enable_shared_from_this<IColumn> {
public:
    virtual ~IColumn() = default;

    // ==================== Type Information ====================

    /**
     * Column type name (e.g., "UInt32", "String")
     */
    virtual std::string getName() const = 0;

    /**
     * Get type index for fast comparison
     */
    virtual TypeIndex getDataType() const = 0;

    // ==================== Size ====================

    /**
     * Number of rows in column
     */
    virtual size_t size() const = 0;

    /**
     * Allocated memory in bytes
     */
    virtual size_t byteSize() const = 0;

    /**
     * Check if column is empty
     */
    bool empty() const { return size() == 0; }

    // ==================== Data Access ====================

    /**
     * Get element as Field (variant type)
     */
    virtual Field operator[](size_t n) const = 0;

    /**
     * Get element at position
     */
    virtual void get(size_t n, Field& res) const = 0;

    /**
     * Get raw data pointer (if column is contiguous)
     * Returns nullptr if not applicable
     */
    virtual const char* getRawData() const { return nullptr; }

    /**
     * Is column numeric and contiguous?
     */
    virtual bool isNumeric() const { return false; }

    // ==================== Insertion ====================

    /**
     * Insert value from Field
     */
    virtual void insert(const Field& x) = 0;

    /**
     * Insert value from another column
     */
    virtual void insertFrom(const IColumn& src, size_t n) = 0;

    /**
     * Insert range from another column
     */
    virtual void insertRangeFrom(const IColumn& src, size_t start, size_t length) = 0;

    /**
     * Insert default value
     */
    virtual void insertDefault() = 0;

    /**
     * Insert multiple copies of default
     */
    virtual void insertManyDefaults(size_t length) {
        for (size_t i = 0; i < length; ++i) {
            insertDefault();
        }
    }

    /**
     * Pop last n elements
     */
    virtual void popBack(size_t n) = 0;

    // ==================== Filtering & Slicing ====================

    /**
     * Create column with filtered rows
     * @param filt Bit mask (1 = keep row)
     * @param result_size_hint Expected result size (-1 if unknown)
     */
    virtual ColumnPtr filter(const Filter& filt, ssize_t result_size_hint) const = 0;

    /**
     * Extract range [offset, offset+length)
     */
    virtual ColumnPtr cut(size_t offset, size_t length) const = 0;

    // ==================== Comparison ====================

    /**
     * Compare row n with row m in rhs
     * @return <0 if less, 0 if equal, >0 if greater
     */
    virtual int compareAt(size_t n, size_t m, const IColumn& rhs, int nan_direction_hint) const = 0;

    // ==================== COW Operations ====================

    /**
     * Assume mutable (for in-place modifications)
     * Use after creating new column or calling mutate()
     */
    MutableColumnPtr assumeMutable() {
        return std::const_pointer_cast<IColumn>(shared_from_this());
    }

    /**
     * Create mutable copy if shared, otherwise return this
     */
    MutableColumnPtr mutate() const {
        if (use_count() > 1) {
            return clone();
        }
        return std::const_pointer_cast<IColumn>(shared_from_this());
    }

    // ==================== Cloning ====================

    /**
     * Deep copy
     */
    virtual MutableColumnPtr clone() const = 0;

    /**
     * Clone and resize
     */
    virtual MutableColumnPtr cloneResized(size_t new_size) const = 0;

    /**
     * Clone empty column (same type, zero size)
     */
    virtual MutableColumnPtr cloneEmpty() const = 0;

    // ==================== Utilities ====================

    /**
     * Check if column is ColumnConst
     */
    virtual bool isConst() const { return false; }

    /**
     * Check if column is ColumnNullable
     */
    virtual bool isNullable() const { return false; }

    /**
     * Check if column can be inside Nullable
     */
    virtual bool canBeInsideNullable() const { return true; }

protected:
    /**
     * Reference count (for COW)
     */
    long use_count() const { return shared_from_this().use_count(); }
};

/**
 * Count number of 1s in filter
 */
inline size_t countBytesInFilter(const Filter& filt) {
    size_t count = 0;
    for (uint8_t byte : filt) {
        count += byte;
    }
    return count;
}

}  // namespace columns
}  // namespace diagon
