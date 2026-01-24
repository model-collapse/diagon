// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/skipindex/IMergeTreeIndexGranule.h"

#include <memory>
#include <string>

namespace diagon {
namespace index {
namespace skipindex {

/**
 * Query-time condition evaluation for granule filtering
 *
 * Converts WHERE clause to index-specific representation.
 * Tests each granule for potential matches.
 *
 * Based on: ClickHouse IMergeTreeIndexCondition
 *
 * NOTE: Stub implementation - provides interface only.
 */
class IMergeTreeIndexCondition {
public:
    virtual ~IMergeTreeIndexCondition() = default;

    // ==================== Query Analysis ====================

    /**
     * Can this index help with the query?
     * @return true if index cannot filter any data
     */
    virtual bool alwaysUnknownOrTrue() const = 0;

    // ==================== Granule Filtering ====================

    /**
     * Can data in this granule match query condition?
     *
     * @param granule Index granule metadata
     * @return true if granule MAY contain matching rows (read it)
     *         false if granule CANNOT contain matches (skip it)
     */
    virtual bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr granule) const = 0;

    // ==================== Description ====================

    virtual std::string getDescription() const = 0;
};

using MergeTreeIndexConditionPtr = std::shared_ptr<IMergeTreeIndexCondition>;

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
