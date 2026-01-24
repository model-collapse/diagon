// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace index {
namespace skipindex {

// Format version for backward compatibility
using MergeTreeIndexVersion = uint8_t;

constexpr MergeTreeIndexVersion MINMAX_VERSION_V1 = 1;  // Original
constexpr MergeTreeIndexVersion MINMAX_VERSION_V2 = 2;  // Nullable support
constexpr MergeTreeIndexVersion SET_VERSION_V1 = 1;
constexpr MergeTreeIndexVersion BLOOM_FILTER_VERSION_V1 = 1;

/**
 * Granule-level index data
 *
 * One granule per N data granules (configurable granularity).
 * Serialized to .idx file alongside data part.
 *
 * Based on: ClickHouse IMergeTreeIndexGranule
 *
 * NOTE: Stub implementation - provides interface only.
 */
class IMergeTreeIndexGranule {
public:
    virtual ~IMergeTreeIndexGranule() = default;

    // ==================== Properties ====================

    /**
     * Does this granule contain no data?
     */
    virtual bool empty() const = 0;

    /**
     * Memory footprint in bytes
     */
    virtual size_t memoryUsageBytes() const = 0;
};

using MergeTreeIndexGranulePtr = std::shared_ptr<IMergeTreeIndexGranule>;
using MergeTreeIndexGranules = std::vector<MergeTreeIndexGranulePtr>;

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
