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
 * NumericRangeQuery - Query matching documents with numeric field values in a range
 *
 * Matches documents where field value is in range [lowerValue, upperValue].
 * Endpoints can be excluded via includeLower/includeUpper flags.
 *
 * Uses NumericDocValues for filtering - efficient O(1) per document check.
 *
 * Examples:
 *   price:[100 TO 1000]     -> NumericRangeQuery("price", 100, 1000, true, true)
 *   timestamp:{0 TO 1000}   -> NumericRangeQuery("timestamp", 0, 1000, false, false)
 *   score:[0.5 TO *]        -> NumericRangeQuery("score", 0.5, MAX, true, true)
 *
 * Based on: org.apache.lucene.search.NumericRangeQuery (Lucene 4.x)
 *           org.apache.lucene.search.PointRangeQuery (Lucene 6+)
 *
 * Phase 4 implementation:
 * - Uses NumericDocValues for filtering
 * - Constant score (no BM25)
 * - No point values (more efficient range trees)
 * - No multi-dimensional ranges
 */
class NumericRangeQuery : public Query {
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
    NumericRangeQuery(const std::string& field, int64_t lowerValue, int64_t upperValue,
                      bool includeLower, bool includeUpper);

    /**
     * Create unbounded lower range: field <= upperValue
     */
    static std::unique_ptr<NumericRangeQuery> newUpperBoundQuery(const std::string& field,
                                                                  int64_t upperValue,
                                                                  bool includeUpper);

    /**
     * Create unbounded upper range: field >= lowerValue
     */
    static std::unique_ptr<NumericRangeQuery> newLowerBoundQuery(const std::string& field,
                                                                  int64_t lowerValue,
                                                                  bool includeLower);

    /**
     * Create exact value query: field == value
     */
    static std::unique_ptr<NumericRangeQuery> newExactQuery(const std::string& field,
                                                             int64_t value);

    // ==================== Accessors ====================

    const std::string& getField() const { return field_; }
    int64_t getLowerValue() const { return lowerValue_; }
    int64_t getUpperValue() const { return upperValue_; }
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
    int64_t lowerValue_;
    int64_t upperValue_;
    bool includeLower_;
    bool includeUpper_;
};

}  // namespace search
}  // namespace diagon
