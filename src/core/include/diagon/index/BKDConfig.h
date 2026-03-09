// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <stdexcept>

namespace diagon {
namespace index {

/**
 * BKDConfig - Configuration for BKD tree
 *
 * Based on: org.apache.lucene.util.bkd.BKDConfig
 *
 * Defines the geometry of a BKD tree: number of dimensions,
 * bytes per dimension, and maximum points per leaf node.
 */
struct BKDConfig {
    static constexpr int MAX_DIMS = 16;
    static constexpr int DEFAULT_MAX_POINTS_PER_LEAF = 512;

    int numDims;                // Total number of dimensions
    int numIndexDims;           // Dimensions used for indexing (may be < numDims)
    int bytesPerDim;            // Bytes per dimension value
    int packedBytesLength;      // numDims * bytesPerDim
    int packedIndexBytesLength; // numIndexDims * bytesPerDim
    int maxPointsPerLeaf;       // Maximum points in a leaf block

    BKDConfig() = default;

    BKDConfig(int numDims, int numIndexDims, int bytesPerDim,
              int maxPointsPerLeaf = DEFAULT_MAX_POINTS_PER_LEAF)
        : numDims(numDims)
        , numIndexDims(numIndexDims)
        , bytesPerDim(bytesPerDim)
        , packedBytesLength(numDims * bytesPerDim)
        , packedIndexBytesLength(numIndexDims * bytesPerDim)
        , maxPointsPerLeaf(maxPointsPerLeaf) {
        if (numDims < 1 || numDims > MAX_DIMS) {
            throw std::invalid_argument("numDims must be in [1, " +
                                        std::to_string(MAX_DIMS) + "]");
        }
        if (numIndexDims < 1 || numIndexDims > numDims) {
            throw std::invalid_argument("numIndexDims must be in [1, numDims]");
        }
        if (bytesPerDim < 1) {
            throw std::invalid_argument("bytesPerDim must be >= 1");
        }
        if (maxPointsPerLeaf < 1) {
            throw std::invalid_argument("maxPointsPerLeaf must be >= 1");
        }
    }

    /**
     * Create config for 1D int64 (8 bytes per dim)
     */
    static BKDConfig forLong() {
        return BKDConfig(1, 1, 8);
    }

    /**
     * Create config for 1D double (8 bytes per dim, sortable encoding)
     */
    static BKDConfig forDouble() {
        return BKDConfig(1, 1, 8);
    }
};

}  // namespace index
}  // namespace diagon
