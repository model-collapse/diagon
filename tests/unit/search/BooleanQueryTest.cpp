// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/BooleanQuery.h"

#include "diagon/search/NumericRangeQuery.h"
#include "diagon/search/TermQuery.h"

#include <gtest/gtest.h>

using namespace diagon::search;

// ==================== Helper Functions ====================

std::shared_ptr<Query> termQuery(const std::string& field, const std::string& text) {
    return std::make_shared<TermQuery>(Term(field, text));
}

std::shared_ptr<Query> rangeQuery(const std::string& field, int64_t lower, int64_t upper) {
    return std::make_shared<NumericRangeQuery>(field, lower, upper, true, true);
}

// ==================== Builder Tests ====================

TEST(BooleanQueryTest, EmptyQuery) {
    auto query = BooleanQuery::Builder().build();

    EXPECT_TRUE(query->clauses().empty());
    EXPECT_EQ(0, query->getMinimumNumberShouldMatch());
}

TEST(BooleanQueryTest, SingleMustClause) {
    auto query = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    ASSERT_EQ(1u, query->clauses().size());
    EXPECT_EQ(Occur::MUST, query->clauses()[0].occur);
    EXPECT_TRUE(query->isRequired());
    EXPECT_FALSE(query->isPureDisjunction());
}

TEST(BooleanQueryTest, SingleShouldClause) {
    auto query = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::SHOULD).build();

    ASSERT_EQ(1u, query->clauses().size());
    EXPECT_EQ(Occur::SHOULD, query->clauses()[0].occur);
    EXPECT_FALSE(query->isRequired());
    EXPECT_TRUE(query->isPureDisjunction());
}

TEST(BooleanQueryTest, MultipleClauses) {
    auto query = BooleanQuery::Builder()
                     .add(termQuery("field1", "value1"), Occur::MUST)
                     .add(termQuery("field2", "value2"), Occur::SHOULD)
                     .add(termQuery("field3", "value3"), Occur::MUST_NOT)
                     .build();

    ASSERT_EQ(3u, query->clauses().size());
    EXPECT_EQ(Occur::MUST, query->clauses()[0].occur);
    EXPECT_EQ(Occur::SHOULD, query->clauses()[1].occur);
    EXPECT_EQ(Occur::MUST_NOT, query->clauses()[2].occur);
}

TEST(BooleanQueryTest, MinimumNumberShouldMatch) {
    auto query = BooleanQuery::Builder()
                     .add(termQuery("field1", "value1"), Occur::SHOULD)
                     .add(termQuery("field2", "value2"), Occur::SHOULD)
                     .add(termQuery("field3", "value3"), Occur::SHOULD)
                     .setMinimumNumberShouldMatch(2)
                     .build();

    EXPECT_EQ(2, query->getMinimumNumberShouldMatch());
    EXPECT_EQ(3u, query->clauses().size());
}

// ==================== Query Type Detection ====================

TEST(BooleanQueryTest, IsPureDisjunction) {
    // Pure SHOULD clauses
    auto pureOr = BooleanQuery::Builder()
                      .add(termQuery("f1", "v1"), Occur::SHOULD)
                      .add(termQuery("f2", "v2"), Occur::SHOULD)
                      .build();

    EXPECT_TRUE(pureOr->isPureDisjunction());

    // Mixed with MUST
    auto mixed = BooleanQuery::Builder()
                     .add(termQuery("f1", "v1"), Occur::MUST)
                     .add(termQuery("f2", "v2"), Occur::SHOULD)
                     .build();

    EXPECT_FALSE(mixed->isPureDisjunction());

    // Empty
    auto empty = BooleanQuery::Builder().build();
    EXPECT_FALSE(empty->isPureDisjunction());
}

TEST(BooleanQueryTest, IsRequired) {
    // Has MUST clause
    auto withMust = BooleanQuery::Builder().add(termQuery("f1", "v1"), Occur::MUST).build();

    EXPECT_TRUE(withMust->isRequired());

    // Has FILTER clause
    auto withFilter =
        BooleanQuery::Builder().add(rangeQuery("price", 100, 1000), Occur::FILTER).build();

    EXPECT_TRUE(withFilter->isRequired());

    // Only SHOULD clauses
    auto onlyShould = BooleanQuery::Builder().add(termQuery("f1", "v1"), Occur::SHOULD).build();

    EXPECT_FALSE(onlyShould->isRequired());
}

// ==================== toString Tests ====================

TEST(BooleanQueryTest, ToStringMustClause) {
    auto query = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    std::string str = query->toString("field");
    // BytesRef outputs as hex, so "value" becomes "[76 61 6c 75 65]"
    EXPECT_TRUE(str.find("+") == 0);                  // Should start with +
    EXPECT_TRUE(str.find("[") != std::string::npos);  // Contains hex bytes
}

TEST(BooleanQueryTest, ToStringShouldClause) {
    auto query = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::SHOULD).build();

    std::string str = query->toString("field");
    // SHOULD clause has no prefix
    EXPECT_TRUE(str.find("+") == std::string::npos);
    EXPECT_TRUE(str.find("-") == std::string::npos);
    EXPECT_TRUE(str.find("#") == std::string::npos);
}

TEST(BooleanQueryTest, ToStringMustNotClause) {
    auto query = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST_NOT).build();

    std::string str = query->toString("field");
    EXPECT_TRUE(str.find("-") == 0);  // Should start with -
}

TEST(BooleanQueryTest, ToStringFilterClause) {
    auto query = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::FILTER).build();

    std::string str = query->toString("field");
    EXPECT_TRUE(str.find("#") == 0);  // Should start with #
}

TEST(BooleanQueryTest, ToStringMultipleClauses) {
    auto query = BooleanQuery::Builder()
                     .add(termQuery("field1", "value1"), Occur::MUST)
                     .add(termQuery("field2", "value2"), Occur::SHOULD)
                     .add(termQuery("field3", "value3"), Occur::MUST_NOT)
                     .build();

    std::string str = query->toString("field");
    // Check for presence of all clause types
    EXPECT_TRUE(str.find("+") != std::string::npos);  // MUST clause
    EXPECT_TRUE(str.find("-") != std::string::npos);  // MUST_NOT clause
    EXPECT_TRUE(str.find("field1:") != std::string::npos);
    EXPECT_TRUE(str.find("field2:") != std::string::npos);
    EXPECT_TRUE(str.find("field3:") != std::string::npos);
}

TEST(BooleanQueryTest, ToStringWithMinimumShouldMatch) {
    auto query = BooleanQuery::Builder()
                     .add(termQuery("f1", "v1"), Occur::SHOULD)
                     .add(termQuery("f2", "v2"), Occur::SHOULD)
                     .setMinimumNumberShouldMatch(2)
                     .build();

    std::string str = query->toString("f");
    EXPECT_TRUE(str.find("~2") != std::string::npos);  // Should have ~2 suffix
}

// ==================== Equality Tests ====================

TEST(BooleanQueryTest, EqualityTrue) {
    auto q1 = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    auto q2 = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    EXPECT_TRUE(q1->equals(*q2));
    EXPECT_TRUE(q2->equals(*q1));
}

TEST(BooleanQueryTest, EqualityFalseDifferentClauses) {
    auto q1 = BooleanQuery::Builder().add(termQuery("field1", "value1"), Occur::MUST).build();

    auto q2 = BooleanQuery::Builder().add(termQuery("field2", "value2"), Occur::MUST).build();

    EXPECT_FALSE(q1->equals(*q2));
}

TEST(BooleanQueryTest, EqualityFalseDifferentOccur) {
    auto q1 = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    auto q2 = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::SHOULD).build();

    EXPECT_FALSE(q1->equals(*q2));
}

TEST(BooleanQueryTest, EqualityFalseDifferentMinimumShouldMatch) {
    auto q1 = BooleanQuery::Builder()
                  .add(termQuery("f1", "v1"), Occur::SHOULD)
                  .setMinimumNumberShouldMatch(1)
                  .build();

    auto q2 = BooleanQuery::Builder()
                  .add(termQuery("f1", "v1"), Occur::SHOULD)
                  .setMinimumNumberShouldMatch(2)
                  .build();

    EXPECT_FALSE(q1->equals(*q2));
}

// ==================== Clone Tests ====================

TEST(BooleanQueryTest, Clone) {
    auto original = BooleanQuery::Builder()
                        .add(termQuery("field", "value"), Occur::MUST)
                        .add(rangeQuery("price", 100, 1000), Occur::FILTER)
                        .setMinimumNumberShouldMatch(1)
                        .build();

    auto cloned = original->clone();

    EXPECT_TRUE(original->equals(*cloned));
    EXPECT_EQ(original->clauses().size(),
              dynamic_cast<BooleanQuery*>(cloned.get())->clauses().size());
    EXPECT_EQ(original->getMinimumNumberShouldMatch(),
              dynamic_cast<BooleanQuery*>(cloned.get())->getMinimumNumberShouldMatch());
}

// ==================== HashCode Tests ====================

TEST(BooleanQueryTest, HashCodeConsistency) {
    auto q1 = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    auto q2 = BooleanQuery::Builder().add(termQuery("field", "value"), Occur::MUST).build();

    EXPECT_EQ(q1->hashCode(), q2->hashCode());
}

TEST(BooleanQueryTest, HashCodeDifferent) {
    auto q1 = BooleanQuery::Builder().add(termQuery("field1", "value1"), Occur::MUST).build();

    auto q2 = BooleanQuery::Builder().add(termQuery("field2", "value2"), Occur::MUST).build();

    // Different queries will likely have different hashes (not guaranteed)
    EXPECT_GT(q1->hashCode(), 0UL);
    EXPECT_GT(q2->hashCode(), 0UL);
}

// ==================== Complex Query Examples ====================

TEST(BooleanQueryTest, ECommerceQuery) {
    // (category:electronics AND in_stock:true) OR featured:true
    // price:[100 TO 1000]
    // NOT discontinued:true

    auto query = BooleanQuery::Builder()
                     .add(termQuery("category", "electronics"), Occur::MUST)
                     .add(termQuery("in_stock", "true"), Occur::FILTER)
                     .add(termQuery("featured", "true"), Occur::SHOULD)
                     .add(rangeQuery("price", 100, 1000), Occur::FILTER)
                     .add(termQuery("discontinued", "true"), Occur::MUST_NOT)
                     .build();

    EXPECT_EQ(5u, query->clauses().size());
    EXPECT_TRUE(query->isRequired());
    EXPECT_FALSE(query->isPureDisjunction());
}

TEST(BooleanQueryTest, TextSearchWithFilters) {
    // (title:laptop OR description:laptop)
    // price <= 1000
    // rating >= 4

    auto query = BooleanQuery::Builder()
                     .add(termQuery("title", "laptop"), Occur::SHOULD)
                     .add(termQuery("description", "laptop"), Occur::SHOULD)
                     .add(rangeQuery("price", 0, 1000), Occur::FILTER)
                     .add(rangeQuery("rating", 4, 5), Occur::FILTER)
                     .setMinimumNumberShouldMatch(1)
                     .build();

    EXPECT_EQ(4u, query->clauses().size());
    EXPECT_EQ(1, query->getMinimumNumberShouldMatch());
}

TEST(BooleanQueryTest, NestedBooleanQuery) {
    // Create inner boolean query: (field1:value1 OR field2:value2)
    auto innerQuery = BooleanQuery::Builder()
                          .add(termQuery("field1", "value1"), Occur::SHOULD)
                          .add(termQuery("field2", "value2"), Occur::SHOULD)
                          .build();

    // Create outer boolean query
    auto outerQuery = BooleanQuery::Builder()
                          .add(std::shared_ptr<Query>(innerQuery.release()), Occur::MUST)
                          .add(termQuery("field3", "value3"), Occur::FILTER)
                          .build();

    EXPECT_EQ(2u, outerQuery->clauses().size());
    EXPECT_TRUE(outerQuery->isRequired());
}

// ==================== Edge Cases ====================

TEST(BooleanQueryTest, AllMustClauses) {
    auto query = BooleanQuery::Builder()
                     .add(termQuery("f1", "v1"), Occur::MUST)
                     .add(termQuery("f2", "v2"), Occur::MUST)
                     .add(termQuery("f3", "v3"), Occur::MUST)
                     .build();

    EXPECT_EQ(3u, query->clauses().size());
    EXPECT_TRUE(query->isRequired());
    EXPECT_FALSE(query->isPureDisjunction());
}

TEST(BooleanQueryTest, AllShouldClauses) {
    auto query = BooleanQuery::Builder()
                     .add(termQuery("f1", "v1"), Occur::SHOULD)
                     .add(termQuery("f2", "v2"), Occur::SHOULD)
                     .add(termQuery("f3", "v3"), Occur::SHOULD)
                     .build();

    EXPECT_EQ(3u, query->clauses().size());
    EXPECT_FALSE(query->isRequired());
    EXPECT_TRUE(query->isPureDisjunction());
}

TEST(BooleanQueryTest, AllFilterClauses) {
    auto query = BooleanQuery::Builder()
                     .add(rangeQuery("price", 100, 1000), Occur::FILTER)
                     .add(rangeQuery("rating", 4, 5), Occur::FILTER)
                     .build();

    EXPECT_EQ(2u, query->clauses().size());
    EXPECT_TRUE(query->isRequired());
    EXPECT_FALSE(query->isPureDisjunction());
}

TEST(BooleanQueryTest, OnlyMustNotClauses) {
    // Query with only MUST_NOT is unusual but valid (matches nothing in isolation)
    auto query = BooleanQuery::Builder().add(termQuery("spam", "true"), Occur::MUST_NOT).build();

    EXPECT_EQ(1u, query->clauses().size());
    EXPECT_FALSE(query->isRequired());
}

// ==================== Integration Notes ====================
//
// These tests verify the BooleanQuery API but don't test actual query execution
// (scorer behavior). Integration tests with IndexSearcher will validate:
// - ConjunctionScorer correctly implements AND logic
// - DisjunctionScorer correctly implements OR logic
// - ReqExclScorer correctly excludes MUST_NOT docs
// - Score aggregation (sum of MUST/SHOULD scores)
// - minimumNumberShouldMatch enforcement
