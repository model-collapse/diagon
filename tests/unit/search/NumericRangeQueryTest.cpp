// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/NumericRangeQuery.h"

#include <gtest/gtest.h>
#include <limits>

using namespace diagon::search;

// ==================== Basic Construction Tests ====================

TEST(NumericRangeQueryTest, BasicConstruction) {
    NumericRangeQuery query("price", 100, 1000, true, true);

    EXPECT_EQ("price", query.getField());
    EXPECT_EQ(100, query.getLowerValue());
    EXPECT_EQ(1000, query.getUpperValue());
    EXPECT_TRUE(query.getIncludeLower());
    EXPECT_TRUE(query.getIncludeUpper());
}

TEST(NumericRangeQueryTest, ExclusiveBounds) {
    NumericRangeQuery query("timestamp", 0, 100, false, false);

    EXPECT_EQ("timestamp", query.getField());
    EXPECT_EQ(0, query.getLowerValue());
    EXPECT_EQ(100, query.getUpperValue());
    EXPECT_FALSE(query.getIncludeLower());
    EXPECT_FALSE(query.getIncludeUpper());
}

TEST(NumericRangeQueryTest, InvalidRange) {
    // Lower > upper should throw
    EXPECT_THROW(NumericRangeQuery("field", 100, 50, true, true), std::invalid_argument);
}

// ==================== Factory Methods ====================

TEST(NumericRangeQueryTest, NewUpperBoundQuery) {
    auto query = NumericRangeQuery::newUpperBoundQuery("score", 100, true);

    EXPECT_EQ("score", query->getField());
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), query->getLowerValue());
    EXPECT_EQ(100, query->getUpperValue());
    EXPECT_TRUE(query->getIncludeLower());
    EXPECT_TRUE(query->getIncludeUpper());
}

TEST(NumericRangeQueryTest, NewLowerBoundQuery) {
    auto query = NumericRangeQuery::newLowerBoundQuery("age", 18, true);

    EXPECT_EQ("age", query->getField());
    EXPECT_EQ(18, query->getLowerValue());
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), query->getUpperValue());
    EXPECT_TRUE(query->getIncludeLower());
    EXPECT_TRUE(query->getIncludeUpper());
}

TEST(NumericRangeQueryTest, NewExactQuery) {
    auto query = NumericRangeQuery::newExactQuery("id", 42);

    EXPECT_EQ("id", query->getField());
    EXPECT_EQ(42, query->getLowerValue());
    EXPECT_EQ(42, query->getUpperValue());
    EXPECT_TRUE(query->getIncludeLower());
    EXPECT_TRUE(query->getIncludeUpper());
}

// ==================== toString Tests ====================

TEST(NumericRangeQueryTest, ToStringBasic) {
    NumericRangeQuery query("price", 100, 1000, true, true);
    std::string str = query.toString("price");

    EXPECT_EQ("[100 TO 1000]", str);
}

TEST(NumericRangeQueryTest, ToStringWithFieldPrefix) {
    NumericRangeQuery query("price", 100, 1000, true, true);
    std::string str = query.toString("other_field");

    EXPECT_EQ("price:[100 TO 1000]", str);
}

TEST(NumericRangeQueryTest, ToStringExclusiveBounds) {
    NumericRangeQuery query("timestamp", 0, 100, false, false);
    std::string str = query.toString("timestamp");

    EXPECT_EQ("{0 TO 100}", str);
}

TEST(NumericRangeQueryTest, ToStringMixedBounds) {
    NumericRangeQuery query("score", 50, 100, true, false);
    std::string str = query.toString("score");

    EXPECT_EQ("[50 TO 100}", str);
}

TEST(NumericRangeQueryTest, ToStringUnboundedLower) {
    auto query = NumericRangeQuery::newUpperBoundQuery("price", 1000, true);
    std::string str = query->toString("price");

    EXPECT_EQ("[* TO 1000]", str);
}

TEST(NumericRangeQueryTest, ToStringUnboundedUpper) {
    auto query = NumericRangeQuery::newLowerBoundQuery("price", 100, true);
    std::string str = query->toString("price");

    EXPECT_EQ("[100 TO *]", str);
}

// ==================== Equality Tests ====================

TEST(NumericRangeQueryTest, EqualityTrue) {
    NumericRangeQuery q1("price", 100, 1000, true, true);
    NumericRangeQuery q2("price", 100, 1000, true, true);

    EXPECT_TRUE(q1.equals(q2));
    EXPECT_TRUE(q2.equals(q1));
}

TEST(NumericRangeQueryTest, EqualityFalseDifferentField) {
    NumericRangeQuery q1("price", 100, 1000, true, true);
    NumericRangeQuery q2("cost", 100, 1000, true, true);

    EXPECT_FALSE(q1.equals(q2));
}

TEST(NumericRangeQueryTest, EqualityFalseDifferentValues) {
    NumericRangeQuery q1("price", 100, 1000, true, true);
    NumericRangeQuery q2("price", 200, 1000, true, true);

    EXPECT_FALSE(q1.equals(q2));
}

TEST(NumericRangeQueryTest, EqualityFalseDifferentBounds) {
    NumericRangeQuery q1("price", 100, 1000, true, true);
    NumericRangeQuery q2("price", 100, 1000, false, true);

    EXPECT_FALSE(q1.equals(q2));
}

// ==================== Clone Tests ====================

TEST(NumericRangeQueryTest, Clone) {
    NumericRangeQuery original("price", 100, 1000, true, false);
    auto cloned = original.clone();

    EXPECT_TRUE(original.equals(*cloned));
    EXPECT_EQ(original.getField(), dynamic_cast<NumericRangeQuery*>(cloned.get())->getField());
    EXPECT_EQ(original.getLowerValue(),
              dynamic_cast<NumericRangeQuery*>(cloned.get())->getLowerValue());
    EXPECT_EQ(original.getUpperValue(),
              dynamic_cast<NumericRangeQuery*>(cloned.get())->getUpperValue());
}

// ==================== HashCode Tests ====================

TEST(NumericRangeQueryTest, HashCodeConsistency) {
    NumericRangeQuery q1("price", 100, 1000, true, true);
    NumericRangeQuery q2("price", 100, 1000, true, true);

    // Equal objects should have same hash
    EXPECT_EQ(q1.hashCode(), q2.hashCode());
}

TEST(NumericRangeQueryTest, HashCodeDifferent) {
    NumericRangeQuery q1("price", 100, 1000, true, true);
    NumericRangeQuery q2("price", 200, 1000, true, true);

    // Different objects will likely have different hashes (not guaranteed, but very likely)
    // We don't assert inequality since hash collisions are technically allowed
    // Just verify both hashes are computed without error
    EXPECT_GT(q1.hashCode(), 0UL);
    EXPECT_GT(q2.hashCode(), 0UL);
}

// ==================== Edge Cases ====================

TEST(NumericRangeQueryTest, NegativeRange) {
    NumericRangeQuery query("temperature", -100, -10, true, true);

    EXPECT_EQ(-100, query.getLowerValue());
    EXPECT_EQ(-10, query.getUpperValue());
}

TEST(NumericRangeQueryTest, ZeroCrossingRange) {
    NumericRangeQuery query("balance", -50, 50, true, true);

    EXPECT_EQ(-50, query.getLowerValue());
    EXPECT_EQ(50, query.getUpperValue());
}

TEST(NumericRangeQueryTest, SingleValueRange) {
    NumericRangeQuery query("count", 42, 42, true, true);

    EXPECT_EQ(42, query.getLowerValue());
    EXPECT_EQ(42, query.getUpperValue());
}

TEST(NumericRangeQueryTest, LargeValues) {
    int64_t large = 1000000000000LL;
    NumericRangeQuery query("big_number", large, large + 1000, true, true);

    EXPECT_EQ(large, query.getLowerValue());
    EXPECT_EQ(large + 1000, query.getUpperValue());
}

// ==================== Integration Notes ====================
//
// These tests verify the NumericRangeQuery API but don't test actual query execution.
// Integration tests with IndexSearcher, NumericDocValues, and actual document filtering
// will be added once those components are wired together.
//
// For now, this validates:
// - Construction and accessors
// - Factory methods
// - String representation
// - Equality/hashing
// - Cloning
// - Edge cases
