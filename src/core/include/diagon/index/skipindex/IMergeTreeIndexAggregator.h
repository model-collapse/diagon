// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/skipindex/IMergeTreeIndexGranule.h"

#include <memory>

namespace diagon {
namespace index {
namespace skipindex {

/**
 * Accumulates index data during writes
 *
 * One aggregator per index per segment.
 * Accumulates rows until granularity boundary, then emits granule.
 *
 * Based on: ClickHouse IMergeTreeIndexAggregator
 *
 * NOTE: Stub implementation - provides interface only.
 */
class IMergeTreeIndexAggregator {
public:
    virtual ~IMergeTreeIndexAggregator() = default;

    // ==================== State Management ====================

    /**
     * Has no accumulated data?
     */
    virtual bool empty() const = 0;

    /**
     * Create granule from accumulated data and reset state
     * Called when granularity boundary reached
     */
    virtual MergeTreeIndexGranulePtr getGranuleAndReset() = 0;
};

using MergeTreeIndexAggregatorPtr = std::shared_ptr<IMergeTreeIndexAggregator>;

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
