// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/skipindex/IMergeTreeIndexAggregator.h"
#include "diagon/index/skipindex/IMergeTreeIndexCondition.h"
#include "diagon/index/skipindex/IMergeTreeIndexGranule.h"

#include <memory>
#include <string>

namespace diagon {
namespace index {
namespace skipindex {

/**
 * Index type enumeration
 */
enum class IndexType {
    MINMAX,
    SET,
    BLOOM_FILTER,
    NGRAMBF_V1
};

/**
 * Index description
 */
struct IndexDescription {
    std::string name;
    IndexType type;
    size_t granularity;  // How many data granules per index granule

    IndexDescription(const std::string& name_, IndexType type_, size_t granularity_ = 1)
        : name(name_), type(type_), granularity(granularity_) {}
};

/**
 * Skip index definition and factory
 *
 * Provides factory methods for granules, aggregators, and conditions.
 * Defines file naming and serialization format.
 *
 * Based on: ClickHouse IMergeTreeIndex
 *
 * NOTE: Stub implementation - provides interface only.
 */
class IMergeTreeIndex {
public:
    explicit IMergeTreeIndex(const IndexDescription& index)
        : index_(index) {}

    virtual ~IMergeTreeIndex() = default;

    // ==================== File Naming ====================

    /**
     * Index file name: "skp_idx_<name>.idx"
     */
    std::string getFileName() const {
        return "skp_idx_" + index_.name;
    }

    /**
     * File extension
     */
    virtual std::string getFileExtension() const {
        return ".idx";
    }

    /**
     * How many data granules per index granule
     */
    size_t getGranularity() const {
        return index_.granularity;
    }

    // ==================== Factory Methods ====================

    /**
     * Create empty granule
     */
    virtual MergeTreeIndexGranulePtr createIndexGranule() const = 0;

    /**
     * Create aggregator for building index
     */
    virtual MergeTreeIndexAggregatorPtr createIndexAggregator() const = 0;

    /**
     * Create condition for query filtering
     */
    virtual MergeTreeIndexConditionPtr createIndexCondition() const = 0;

    // ==================== Properties ====================

    const IndexDescription& getIndexDescription() const {
        return index_;
    }

    std::string getName() const {
        return index_.name;
    }

    IndexType getType() const {
        return index_.type;
    }

private:
    IndexDescription index_;
};

using MergeTreeIndexPtr = std::shared_ptr<IMergeTreeIndex>;

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
