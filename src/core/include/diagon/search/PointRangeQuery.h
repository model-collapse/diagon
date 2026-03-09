// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/Weight.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace search {

/**
 * PointRangeQuery - Range query using BKD tree for O(log N) performance
 *
 * Based on: org.apache.lucene.search.PointRangeQuery
 *
 * Uses PointValues::intersect() to traverse the BKD tree, pruning
 * entire subtrees that fall outside the query range. Collects matching
 * doc IDs into a BitSet, then iterates via DocIdSetIterator.
 *
 * Returns constant score (no BM25).
 */
class PointRangeQuery : public Query {
public:
    /**
     * Create a point range query on packed byte values.
     *
     * @param field Field name
     * @param lowerPoint Lower bound (inclusive), packed bytes
     * @param upperPoint Upper bound (inclusive), packed bytes
     * @param numDims Number of dimensions
     * @param bytesPerDim Bytes per dimension
     */
    PointRangeQuery(std::string field, std::vector<uint8_t> lowerPoint,
                    std::vector<uint8_t> upperPoint, int numDims, int bytesPerDim);

    /**
     * Convenience factory for int64 range
     */
    static std::unique_ptr<PointRangeQuery> newLongRange(const std::string& field, int64_t lower,
                                                          int64_t upper);

    /**
     * Convenience factory for double range
     */
    static std::unique_ptr<PointRangeQuery> newDoubleRange(const std::string& field, double lower,
                                                            double upper);

    // ==================== Accessors ====================

    const std::string& getField() const { return field_; }
    const std::vector<uint8_t>& getLowerPoint() const { return lowerPoint_; }
    const std::vector<uint8_t>& getUpperPoint() const { return upperPoint_; }
    int getNumDims() const { return numDims_; }
    int getBytesPerDim() const { return bytesPerDim_; }

    // ==================== Query Interface ====================

    std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                         float boost) const override;
    std::string toString(const std::string& field) const override;
    bool equals(const Query& other) const override;
    size_t hashCode() const override;
    std::unique_ptr<Query> clone() const override;

private:
    std::string field_;
    std::vector<uint8_t> lowerPoint_;
    std::vector<uint8_t> upperPoint_;
    int numDims_;
    int bytesPerDim_;
};

}  // namespace search
}  // namespace diagon
