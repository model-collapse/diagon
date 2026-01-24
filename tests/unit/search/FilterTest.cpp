// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/Filter.h"

#include "diagon/search/BooleanClause.h"
#include "diagon/search/DocIdSet.h"
#include "diagon/search/DocIdSetIterator.h"
#include "diagon/search/Query.h"
#include "diagon/search/Weight.h"

#include <gtest/gtest.h>

using namespace diagon::search;

// ==================== Mock Implementations ====================

class MockDocIdSetIterator : public DocIdSetIterator {
public:
    MockDocIdSetIterator()
        : current_(-1) {}

    int docID() const override { return current_; }

    int nextDoc() override {
        if (current_ == NO_MORE_DOCS)
            return NO_MORE_DOCS;
        current_++;
        if (current_ >= 5) {
            current_ = NO_MORE_DOCS;
        }
        return current_;
    }

    int advance(int target) override {
        while (current_ < target && current_ != NO_MORE_DOCS) {
            nextDoc();
        }
        return current_;
    }

    int64_t cost() const override { return 5; }

private:
    int current_;
};

class MockDocIdSet : public DocIdSet {
public:
    std::unique_ptr<DocIdSetIterator> iterator() const override {
        return std::make_unique<MockDocIdSetIterator>();
    }

    size_t ramBytesUsed() const override { return 1024; }

    bool isCacheable() const override { return true; }
};

class MockFilter : public Filter {
public:
    explicit MockFilter(bool cacheable = true)
        : cacheable_(cacheable) {}

    std::unique_ptr<DocIdSet>
    getDocIdSet(const diagon::index::LeafReaderContext& context) const override {
        return std::make_unique<MockDocIdSet>();
    }

    std::string getCacheKey() const override { return cacheable_ ? "mock_filter_key" : ""; }

    std::string toString() const override { return "MockFilter"; }

    bool equals(const Filter& other) const override {
        return dynamic_cast<const MockFilter*>(&other) != nullptr;
    }

    size_t hashCode() const override { return 12345; }

private:
    bool cacheable_;
};

class MockQuery : public Query {
public:
    std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                         float boost) const override {
        return nullptr;  // Not needed for this test
    }

    std::string toString(const std::string& field) const override { return "MockQuery"; }

    bool equals(const Query& other) const override {
        return dynamic_cast<const MockQuery*>(&other) != nullptr;
    }

    size_t hashCode() const override { return 999; }

    std::unique_ptr<Query> clone() const override { return std::make_unique<MockQuery>(); }
};

// ==================== BooleanClause Tests ====================

TEST(OccurTest, Values) {
    EXPECT_EQ(0, static_cast<uint8_t>(Occur::MUST));
    EXPECT_EQ(1, static_cast<uint8_t>(Occur::SHOULD));
    EXPECT_EQ(2, static_cast<uint8_t>(Occur::MUST_NOT));
    EXPECT_EQ(3, static_cast<uint8_t>(Occur::FILTER));
}

TEST(BooleanClauseTest, Construction) {
    auto query = std::make_shared<MockQuery>();
    BooleanClause clause(query, Occur::MUST);

    EXPECT_EQ(query.get(), clause.query.get());
    EXPECT_EQ(Occur::MUST, clause.occur);
}

TEST(BooleanClauseTest, IsScoring) {
    auto query = std::make_shared<MockQuery>();

    BooleanClause must_clause(query, Occur::MUST);
    EXPECT_TRUE(must_clause.isScoring());

    BooleanClause should_clause(query, Occur::SHOULD);
    EXPECT_TRUE(should_clause.isScoring());

    BooleanClause filter_clause(query, Occur::FILTER);
    EXPECT_FALSE(filter_clause.isScoring());

    BooleanClause must_not_clause(query, Occur::MUST_NOT);
    EXPECT_FALSE(must_not_clause.isScoring());
}

TEST(BooleanClauseTest, IsProhibited) {
    auto query = std::make_shared<MockQuery>();

    BooleanClause must_not_clause(query, Occur::MUST_NOT);
    EXPECT_TRUE(must_not_clause.isProhibited());

    BooleanClause must_clause(query, Occur::MUST);
    EXPECT_FALSE(must_clause.isProhibited());

    BooleanClause should_clause(query, Occur::SHOULD);
    EXPECT_FALSE(should_clause.isProhibited());

    BooleanClause filter_clause(query, Occur::FILTER);
    EXPECT_FALSE(filter_clause.isProhibited());
}

TEST(BooleanClauseTest, IsRequired) {
    auto query = std::make_shared<MockQuery>();

    BooleanClause must_clause(query, Occur::MUST);
    EXPECT_TRUE(must_clause.isRequired());

    BooleanClause filter_clause(query, Occur::FILTER);
    EXPECT_TRUE(filter_clause.isRequired());

    BooleanClause should_clause(query, Occur::SHOULD);
    EXPECT_FALSE(should_clause.isRequired());

    BooleanClause must_not_clause(query, Occur::MUST_NOT);
    EXPECT_FALSE(must_not_clause.isRequired());
}

TEST(BooleanClauseTest, IsFilter) {
    auto query = std::make_shared<MockQuery>();

    BooleanClause filter_clause(query, Occur::FILTER);
    EXPECT_TRUE(filter_clause.isFilter());

    BooleanClause must_clause(query, Occur::MUST);
    EXPECT_FALSE(must_clause.isFilter());

    BooleanClause should_clause(query, Occur::SHOULD);
    EXPECT_FALSE(should_clause.isFilter());

    BooleanClause must_not_clause(query, Occur::MUST_NOT);
    EXPECT_FALSE(must_not_clause.isFilter());
}

// ==================== DocIdSet Tests ====================

TEST(DocIdSetTest, Iterator) {
    MockDocIdSet doc_id_set;

    auto it = doc_id_set.iterator();
    ASSERT_NE(nullptr, it);

    EXPECT_EQ(-1, it->docID());
    EXPECT_EQ(0, it->nextDoc());
    EXPECT_EQ(1, it->nextDoc());
}

TEST(DocIdSetTest, RamBytesUsed) {
    MockDocIdSet doc_id_set;

    EXPECT_EQ(1024, doc_id_set.ramBytesUsed());
}

TEST(DocIdSetTest, IsCacheable) {
    MockDocIdSet doc_id_set;

    EXPECT_TRUE(doc_id_set.isCacheable());
}

// ==================== Filter Tests ====================

TEST(FilterTest, ToString) {
    MockFilter filter;

    EXPECT_EQ("MockFilter", filter.toString());
}

TEST(FilterTest, Equals) {
    MockFilter filter1;
    MockFilter filter2;

    EXPECT_TRUE(filter1.equals(filter2));
}

TEST(FilterTest, HashCode) {
    MockFilter filter;

    EXPECT_EQ(12345, filter.hashCode());
}

TEST(FilterTest, CacheableTrue) {
    MockFilter filter(true);

    EXPECT_TRUE(filter.isCacheable());
    EXPECT_EQ("mock_filter_key", filter.getCacheKey());
}

TEST(FilterTest, CacheableFalse) {
    MockFilter filter(false);

    EXPECT_FALSE(filter.isCacheable());
    EXPECT_EQ("", filter.getCacheKey());
}

// ==================== Integration Tests ====================

TEST(FilterIntegrationTest, FilterVsMustClause) {
    auto query = std::make_shared<MockQuery>();

    BooleanClause must_clause(query, Occur::MUST);
    BooleanClause filter_clause(query, Occur::FILTER);

    // Both are required
    EXPECT_TRUE(must_clause.isRequired());
    EXPECT_TRUE(filter_clause.isRequired());

    // But only MUST participates in scoring
    EXPECT_TRUE(must_clause.isScoring());
    EXPECT_FALSE(filter_clause.isScoring());

    // Only FILTER is marked as filter
    EXPECT_FALSE(must_clause.isFilter());
    EXPECT_TRUE(filter_clause.isFilter());
}
