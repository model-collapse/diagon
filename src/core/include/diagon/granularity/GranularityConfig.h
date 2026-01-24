// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/granularity/IMergeTreeIndexGranularity.h"
#include "diagon/granularity/MergeTreeIndexGranularityAdaptive.h"
#include "diagon/granularity/MergeTreeIndexGranularityConstant.h"

#include <memory>

namespace diagon {
namespace granularity {

/**
 * Configuration for adaptive granularity
 */
struct GranularityConfig {
    /**
     * Target granule size (default: 8192 rows)
     */
    size_t index_granularity = 8192;

    /**
     * Target uncompressed bytes per granule (default: 10MB)
     * Set to 0 to disable adaptive granularity
     */
    size_t index_granularity_bytes = 10 * 1024 * 1024;

    /**
     * Minimum rows per granule (default: 1024)
     * Even with adaptive granularity, don't go below this
     */
    size_t min_index_granularity_bytes = 1024;

    /**
     * Use adaptive granularity?
     */
    bool use_adaptive_granularity() const { return index_granularity_bytes > 0; }

    /**
     * Create appropriate granularity object
     */
    MergeTreeIndexGranularityPtr createGranularity() const {
        if (use_adaptive_granularity()) {
            return std::make_shared<MergeTreeIndexGranularityAdaptive>();
        } else {
            return std::make_shared<MergeTreeIndexGranularityConstant>(index_granularity);
        }
    }
};

/**
 * Helper for writing data with granules
 */
class GranuleWriter {
public:
    explicit GranuleWriter(const GranularityConfig& config)
        : config_(config)
        , granularity_(config.createGranularity()) {}

    /**
     * Check if should finish current granule
     */
    bool shouldFinishGranule(size_t rows_written_in_granule,
                             size_t bytes_written_in_granule) const {
        if (config_.use_adaptive_granularity()) {
            // Adaptive: check both rows and bytes
            return bytes_written_in_granule >= config_.index_granularity_bytes ||
                   rows_written_in_granule >= config_.index_granularity;
        } else {
            // Constant: only check rows
            return rows_written_in_granule >= config_.index_granularity;
        }
    }

    /**
     * Finish granule and add mark
     */
    void finishGranule(size_t rows_in_granule) { granularity_->addMark(rows_in_granule); }

    const IMergeTreeIndexGranularity& getGranularity() const { return *granularity_; }

    MergeTreeIndexGranularityPtr getGranularityPtr() const { return granularity_; }

private:
    GranularityConfig config_;
    MergeTreeIndexGranularityPtr granularity_;
};

}  // namespace granularity
}  // namespace diagon
