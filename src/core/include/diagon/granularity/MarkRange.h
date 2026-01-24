// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/granularity/IMergeTreeIndexGranularity.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace diagon {
namespace granularity {

/**
 * Range of marks to read
 *
 * Defines a contiguous range [begin, end) of marks/granules.
 *
 * Based on: ClickHouse MarkRange
 */
struct MarkRange {
    size_t begin;  // Inclusive
    size_t end;    // Exclusive

    MarkRange()
        : begin(0)
        , end(0) {}

    MarkRange(size_t begin_, size_t end_)
        : begin(begin_)
        , end(end_) {}

    bool operator==(const MarkRange& other) const {
        return begin == other.begin && end == other.end;
    }

    bool operator!=(const MarkRange& other) const { return !(*this == other); }

    bool operator<(const MarkRange& other) const {
        return begin < other.begin || (begin == other.begin && end < other.end);
    }

    size_t getNumberOfMarks() const { return end > begin ? end - begin : 0; }

    bool empty() const { return begin >= end; }
};

using MarkRanges = std::vector<MarkRange>;

/**
 * Convert mark ranges to row ranges
 */
inline std::vector<std::pair<size_t, size_t>>
markRangesToRows(const MarkRanges& mark_ranges, const IMergeTreeIndexGranularity& granularity) {
    std::vector<std::pair<size_t, size_t>> row_ranges;
    row_ranges.reserve(mark_ranges.size());

    for (const auto& range : mark_ranges) {
        if (range.empty()) {
            continue;
        }

        size_t start_row = granularity.getRowsCountInRange(range.begin);
        size_t end_row = granularity.getRowsCountInRange(range.end);

        row_ranges.emplace_back(start_row, end_row);
    }

    return row_ranges;
}

}  // namespace granularity
}  // namespace diagon
