// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/granularity/IMergeTreeIndexGranularity.h"

#include <stdexcept>

namespace diagon {
namespace granularity {

/**
 * Constant granularity: fixed rows per mark
 *
 * Used when: index_granularity_bytes = 0 (adaptive disabled)
 * Default: 8192 rows per mark
 *
 * Based on: ClickHouse MergeTreeIndexGranularityConstant
 */
class MergeTreeIndexGranularityConstant : public IMergeTreeIndexGranularity {
public:
    explicit MergeTreeIndexGranularityConstant(size_t granularity = 8192, size_t num_marks = 0)
        : granularity_(granularity)
        , num_marks_(num_marks) {}

    size_t getMarksCount() const override { return num_marks_; }

    size_t getMarkRows(size_t mark_index) const override {
        if (mark_index >= num_marks_) {
            throw std::out_of_range("Mark index out of range");
        }

        // All marks have granularity_ rows in constant granularity
        return granularity_;
    }

    size_t getRowsCountInRange(size_t begin, size_t end) const override {
        if (end <= begin)
            return 0;
        if (end > num_marks_)
            end = num_marks_;
        if (begin >= num_marks_)
            return 0;

        size_t rows = 0;

        // Full marks
        if (end > begin + 1) {
            rows += (end - begin - 1) * granularity_;
        }

        // Last mark in range (may be partial)
        if (end > begin) {
            rows += getMarkRows(end - 1);
        }

        return rows;
    }

    size_t getMarkContainingRow(size_t row) const override {
        size_t mark = row / granularity_;
        if (mark >= num_marks_) {
            throw std::out_of_range("Row out of range");
        }
        return mark;
    }

    size_t countMarksForRows(size_t from_mark, size_t number_of_rows) const override {
        if (from_mark >= num_marks_)
            return 0;

        size_t from_row = from_mark * granularity_;
        size_t to_row = from_row + number_of_rows;
        size_t to_mark = (to_row + granularity_ - 1) / granularity_;

        if (to_mark > num_marks_) {
            to_mark = num_marks_;
        }

        return to_mark - from_mark;
    }

    bool hasFinalMark() const override {
        return false;  // Constant granularity doesn't need final mark
    }

    /**
     * Add mark (during writing)
     */
    void addMark(size_t rows) override {
        // For constant granularity, we expect granularity_ rows
        // (but allow any value for flexibility)
        ++num_marks_;
    }

    /**
     * Get granularity (rows per mark)
     */
    size_t getGranularity() const { return granularity_; }

private:
    size_t granularity_;  // Rows per mark (e.g., 8192)
    size_t num_marks_;    // Number of marks
};

}  // namespace granularity
}  // namespace diagon
