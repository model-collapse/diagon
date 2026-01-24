// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace diagon {
namespace granularity {

/**
 * Index granularity defines row distribution across marks.
 *
 * Two implementations:
 * - Constant: Fixed rows per mark (e.g., 8192)
 * - Adaptive: Variable rows based on compressed size
 *
 * Based on: ClickHouse IMergeTreeIndexGranularity
 */
class IMergeTreeIndexGranularity {
public:
    virtual ~IMergeTreeIndexGranularity() = default;

    /**
     * Number of marks in this granularity
     */
    virtual size_t getMarksCount() const = 0;

    /**
     * Get rows in specific mark/granule
     */
    virtual size_t getMarkRows(size_t mark_index) const = 0;

    /**
     * Get total rows in range [begin, end)
     */
    virtual size_t getRowsCountInRange(size_t begin, size_t end) const = 0;

    /**
     * Get total rows from start to mark
     */
    size_t getRowsCountInRange(size_t end) const { return getRowsCountInRange(0, end); }

    /**
     * Total rows across all marks
     */
    size_t getTotalRows() const {
        if (getMarksCount() == 0) {
            return 0;
        }
        return getRowsCountInRange(getMarksCount());
    }

    /**
     * Find mark containing row
     * @return mark index
     */
    virtual size_t getMarkContainingRow(size_t row) const = 0;

    /**
     * Get number of marks needed for rows count
     */
    virtual size_t countMarksForRows(size_t from_mark, size_t number_of_rows) const = 0;

    /**
     * Has final mark (empty mark at end)?
     */
    virtual bool hasFinalMark() const = 0;

    /**
     * Is empty (no marks)?
     */
    bool empty() const { return getMarksCount() == 0; }

    /**
     * Add mark (during writing)
     */
    virtual void addMark(size_t rows) = 0;
};

using MergeTreeIndexGranularityPtr = std::shared_ptr<IMergeTreeIndexGranularity>;

}  // namespace granularity
}  // namespace diagon
