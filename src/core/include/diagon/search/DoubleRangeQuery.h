// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/Weight.h"

#include <limits>
#include <memory>
#include <string>

namespace diagon {
namespace search {

/**
 * DoubleRangeQuery - Query matching documents with double field values in a range
 *
 * Matches documents where field value is in range [lowerValue, upperValue].
 * Endpoints can be excluded via includeLower/includeUpper flags.
 *
 * Uses NumericDocValues (double) for filtering - efficient O(1) per document check.
 *
 * Examples:
 *   price:[99.99 TO 999.99]     -> DoubleRangeQuery("price", 99.99, 999.99, true, true)
 *   score:{0.5 TO 1.0}          -> DoubleRangeQuery("score", 0.5, 1.0, false, false)
 *   temperature:[0.0 TO *]      -> DoubleRangeQuery("temperature", 0.0, MAX, true, true)
 *
 * Based on: org.apache.lucene.search.NumericRangeQuery (Lucene 4.x)
 *           org.apache.lucene.search.PointRangeQuery (Lucene 6+)
 *
 * Phase 4 implementation:
 * - Uses NumericDocValues (double) for filtering
 * - Constant score (no BM25)
 * - No point values (more efficient range trees)
 * - No multi-dimensional ranges
 */
class DoubleRangeQuery : public Query {
public:
    /**
     * Constructor for bounded range
     *
     * @param field Field name
     * @param lowerValue Lower bound (inclusive if includeLower=true)
     * @param upperValue Upper bound (inclusive if includeUpper=true)
     * @param includeLower Include lower bound?
     * @param includeUpper Include upper bound?
     */
    DoubleRangeQuery(const std::string& field, double lowerValue, double upperValue,
                     bool includeLower, bool includeUpper);

    /**
     * Create unbounded lower range: field <= upperValue
     */
    static std::unique_ptr<DoubleRangeQuery>
    newUpperBoundQuery(const std::string& field, double upperValue, bool includeUpper);

    /**
     * Create unbounded upper range: field >= lowerValue
     */
    static std::unique_ptr<DoubleRangeQuery>
    newLowerBoundQuery(const std::string& field, double lowerValue, bool includeLower);

    /**
     * Create exact value query: field == value
     */
    static std::unique_ptr<DoubleRangeQuery> newExactQuery(const std::string& field, double value);

    // ==================== Accessors ====================

    const std::string& getField() const { return field_; }
    double getLowerValue() const { return lowerValue_; }
    double getUpperValue() const { return upperValue_; }
    bool getIncludeLower() const { return includeLower_; }
    bool getIncludeUpper() const { return includeUpper_; }

    // ==================== Query Interface ====================

    std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                         float boost) const override;

    std::string toString(const std::string& field) const override;

    bool equals(const Query& other) const override;

    size_t hashCode() const override;

    std::unique_ptr<Query> clone() const override;

private:
    std::string field_;
    double lowerValue_;
    double upperValue_;
    bool includeLower_;
    bool includeUpper_;
};

}  // namespace search
}  // namespace diagon
