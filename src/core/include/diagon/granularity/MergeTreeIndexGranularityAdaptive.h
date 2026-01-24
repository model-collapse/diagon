// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/granularity/IMergeTreeIndexGranularity.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace diagon {
namespace granularity {

/**
 * Adaptive granularity: variable rows per mark
 *
 * Used when: index_granularity_bytes > 0 (default: 10MB)
 * Adjusts granule size to target compressed size.
 *
 * Benefits:
 * - Consistent I/O per granule
 * - Better for large/sparse columns
 *
 * Based on: ClickHouse MergeTreeIndexGranularityAdaptive
 */
class MergeTreeIndexGranularityAdaptive : public IMergeTreeIndexGranularity {
public:
    MergeTreeIndexGranularityAdaptive() = default;

    size_t getMarksCount() const override { return marks_rows_partial_sums_.size(); }

    size_t getMarkRows(size_t mark_index) const override {
        if (mark_index >= marks_rows_partial_sums_.size()) {
            throw std::out_of_range("Mark index out of range");
        }

        if (mark_index == 0) {
            return marks_rows_partial_sums_[0];
        }

        return marks_rows_partial_sums_[mark_index] - marks_rows_partial_sums_[mark_index - 1];
    }

    size_t getRowsCountInRange(size_t begin, size_t end) const override {
        if (end <= begin)
            return 0;
        if (end > marks_rows_partial_sums_.size()) {
            end = marks_rows_partial_sums_.size();
        }
        if (begin >= marks_rows_partial_sums_.size()) {
            return 0;
        }

        size_t end_rows = marks_rows_partial_sums_[end - 1];
        size_t begin_rows = begin == 0 ? 0 : marks_rows_partial_sums_[begin - 1];

        return end_rows - begin_rows;
    }

    size_t getMarkContainingRow(size_t row) const override {
        if (marks_rows_partial_sums_.empty()) {
            throw std::out_of_range("No marks in granularity");
        }

        // Binary search in cumulative sums
        auto it = std::upper_bound(marks_rows_partial_sums_.begin(), marks_rows_partial_sums_.end(),
                                   row);

        if (it == marks_rows_partial_sums_.end()) {
            throw std::out_of_range("Row out of range");
        }

        return std::distance(marks_rows_partial_sums_.begin(), it);
    }

    size_t countMarksForRows(size_t from_mark, size_t number_of_rows) const override {
        if (from_mark >= marks_rows_partial_sums_.size()) {
            return 0;
        }

        size_t rows_before = from_mark == 0 ? 0 : marks_rows_partial_sums_[from_mark - 1];
        size_t target_row = rows_before + number_of_rows;

        // Find first mark that ends at or after target_row
        auto it = std::lower_bound(marks_rows_partial_sums_.begin() + from_mark,
                                   marks_rows_partial_sums_.end(), target_row);

        if (it == marks_rows_partial_sums_.end()) {
            // Target is beyond all marks
            return marks_rows_partial_sums_.size() - from_mark;
        }

        // Distance gives us the number of marks from from_mark to the found mark (exclusive)
        // We need to include the found mark, so add 1
        return std::distance(marks_rows_partial_sums_.begin() + from_mark, it) + 1;
    }

    bool hasFinalMark() const override {
        return !marks_rows_partial_sums_.empty() &&
               getMarkRows(marks_rows_partial_sums_.size() - 1) == 0;
    }

    /**
     * Add mark with specific row count
     */
    void addMark(size_t rows) override {
        size_t cumulative = marks_rows_partial_sums_.empty()
                                ? rows
                                : marks_rows_partial_sums_.back() + rows;

        marks_rows_partial_sums_.push_back(cumulative);
    }

    /**
     * Get cumulative rows at mark
     */
    size_t getCumulativeRows(size_t mark_index) const {
        if (mark_index >= marks_rows_partial_sums_.size()) {
            throw std::out_of_range("Mark index out of range");
        }
        return marks_rows_partial_sums_[mark_index];
    }

private:
    /**
     * Cumulative row counts
     * marks_rows_partial_sums_[i] = total rows from start to end of mark i
     *
     * Example: [100, 250, 408, 550]
     *   Mark 0: 100 rows
     *   Mark 1: 150 rows (250 - 100)
     *   Mark 2: 158 rows (408 - 250)
     *   Mark 3: 142 rows (550 - 408)
     */
    std::vector<size_t> marks_rows_partial_sums_;
};

}  // namespace granularity
}  // namespace diagon
