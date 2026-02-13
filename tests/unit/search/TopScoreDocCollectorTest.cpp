// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/TopScoreDocCollector.h"

#include "diagon/index/LeafReaderContext.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace diagon::search;
using namespace diagon::index;

// ==================== Mock Scorable ====================

class MockScorable : public Scorable {
public:
    MockScorable()
        : currentDoc_(-1)
        , currentScore_(0.0f) {}

    void setDoc(int doc, float score) {
        currentDoc_ = doc;
        currentScore_ = score;
    }

    float score() override { return currentScore_; }

    int docID() override { return currentDoc_; }

private:
    int currentDoc_;
    float currentScore_;
};

// ==================== Helper Functions ====================

LeafReaderContext createContext(int docBase) {
    return LeafReaderContext(nullptr, docBase, 0);
}

// ==================== Basic Tests ====================

TEST(TopScoreDocCollectorTest, CreateCollector) {
    auto collector = TopScoreDocCollector::create(10);
    EXPECT_NE(nullptr, collector);
    // Default threshold=1000 enables TOP_SCORES for WAND early termination
    EXPECT_EQ(ScoreMode::TOP_SCORES, collector->scoreMode());
}

TEST(TopScoreDocCollectorTest, InvalidNumHits) {
    EXPECT_THROW(TopScoreDocCollector::create(0), std::invalid_argument);
    EXPECT_THROW(TopScoreDocCollector::create(-1), std::invalid_argument);
}

TEST(TopScoreDocCollectorTest, EmptyResults) {
    auto collector = TopScoreDocCollector::create(10);
    TopDocs results = collector->topDocs();

    EXPECT_EQ(0, results.totalHits.value);
    EXPECT_EQ(TotalHits::Relation::EQUAL_TO, results.totalHits.relation);
    EXPECT_TRUE(results.scoreDocs.empty());
    EXPECT_TRUE(std::isnan(results.maxScore));
}

TEST(TopScoreDocCollectorTest, SingleDoc) {
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);

    // Get leaf collector
    LeafCollector* leaf = collector->getLeafCollector(context);
    EXPECT_NE(nullptr, leaf);

    // Set scorer
    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect one doc
    scorer.setDoc(5, 1.5f);
    leaf->collect(5);

    // Get results
    TopDocs results = collector->topDocs();
    EXPECT_EQ(1, results.totalHits.value);
    ASSERT_EQ(1, results.scoreDocs.size());
    EXPECT_EQ(5, results.scoreDocs[0].doc);
    EXPECT_FLOAT_EQ(1.5f, results.scoreDocs[0].score);
    EXPECT_FLOAT_EQ(1.5f, results.maxScore);
}

TEST(TopScoreDocCollectorTest, MultipleDocs) {
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect multiple docs
    scorer.setDoc(0, 1.0f);
    leaf->collect(0);

    scorer.setDoc(1, 2.0f);
    leaf->collect(1);

    scorer.setDoc(2, 0.5f);
    leaf->collect(2);

    // Get results (sorted by score descending)
    TopDocs results = collector->topDocs();
    EXPECT_EQ(3, results.totalHits.value);
    ASSERT_EQ(3, results.scoreDocs.size());

    // Check ordering: score descending
    EXPECT_EQ(1, results.scoreDocs[0].doc);
    EXPECT_FLOAT_EQ(2.0f, results.scoreDocs[0].score);

    EXPECT_EQ(0, results.scoreDocs[1].doc);
    EXPECT_FLOAT_EQ(1.0f, results.scoreDocs[1].score);

    EXPECT_EQ(2, results.scoreDocs[2].doc);
    EXPECT_FLOAT_EQ(0.5f, results.scoreDocs[2].score);

    EXPECT_FLOAT_EQ(2.0f, results.maxScore);
}

TEST(TopScoreDocCollectorTest, TopKLimiting) {
    // Only keep top 3 results
    auto collector = TopScoreDocCollector::create(3);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect 5 docs with different scores
    scorer.setDoc(0, 1.0f);
    leaf->collect(0);

    scorer.setDoc(1, 5.0f);
    leaf->collect(1);

    scorer.setDoc(2, 3.0f);
    leaf->collect(2);

    scorer.setDoc(3, 2.0f);
    leaf->collect(3);

    scorer.setDoc(4, 4.0f);
    leaf->collect(4);

    // Get results - should only have top 3
    TopDocs results = collector->topDocs();
    EXPECT_EQ(5, results.totalHits.value);   // Total hits tracked
    ASSERT_EQ(3, results.scoreDocs.size());  // But only top 3 returned

    // Check we got the top 3
    EXPECT_EQ(1, results.scoreDocs[0].doc);
    EXPECT_FLOAT_EQ(5.0f, results.scoreDocs[0].score);

    EXPECT_EQ(4, results.scoreDocs[1].doc);
    EXPECT_FLOAT_EQ(4.0f, results.scoreDocs[1].score);

    EXPECT_EQ(2, results.scoreDocs[2].doc);
    EXPECT_FLOAT_EQ(3.0f, results.scoreDocs[2].score);
}

TEST(TopScoreDocCollectorTest, TieBreaking) {
    // When scores are equal, lower doc ID wins
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // All same score, different doc IDs
    scorer.setDoc(5, 1.0f);
    leaf->collect(5);

    scorer.setDoc(2, 1.0f);
    leaf->collect(2);

    scorer.setDoc(8, 1.0f);
    leaf->collect(8);

    // Get results
    TopDocs results = collector->topDocs();
    ASSERT_EQ(3, results.scoreDocs.size());

    // Should be ordered by doc ID when scores are equal
    EXPECT_EQ(2, results.scoreDocs[0].doc);
    EXPECT_EQ(5, results.scoreDocs[1].doc);
    EXPECT_EQ(8, results.scoreDocs[2].doc);
}

// ==================== Pagination Tests ====================

TEST(TopScoreDocCollectorTest, SearchAfterBasic) {
    // First search - get top 3
    auto collector1 = TopScoreDocCollector::create(3);
    auto context = createContext(0);
    LeafCollector* leaf1 = collector1->getLeafCollector(context);

    MockScorable scorer;
    leaf1->setScorer(&scorer);

    // Collect 6 docs
    for (int i = 0; i < 6; i++) {
        scorer.setDoc(i, 6.0f - i);  // Scores: 6, 5, 4, 3, 2, 1
        leaf1->collect(i);
    }

    TopDocs results1 = collector1->topDocs();
    ASSERT_EQ(3, results1.scoreDocs.size());
    EXPECT_EQ(0, results1.scoreDocs[0].doc);  // score 6
    EXPECT_EQ(1, results1.scoreDocs[1].doc);  // score 5
    EXPECT_EQ(2, results1.scoreDocs[2].doc);  // score 4

    // Second search - get next 3 after last result
    ScoreDoc after = results1.scoreDocs.back();  // doc 2, score 4
    auto collector2 = TopScoreDocCollector::create(3, after);
    LeafCollector* leaf2 = collector2->getLeafCollector(context);
    leaf2->setScorer(&scorer);

    // Collect same docs again
    for (int i = 0; i < 6; i++) {
        scorer.setDoc(i, 6.0f - i);
        leaf2->collect(i);
    }

    TopDocs results2 = collector2->topDocs();
    ASSERT_EQ(3, results2.scoreDocs.size());

    // Should get docs 3, 4, 5 (scores 3, 2, 1)
    EXPECT_EQ(3, results2.scoreDocs[0].doc);
    EXPECT_EQ(4, results2.scoreDocs[1].doc);
    EXPECT_EQ(5, results2.scoreDocs[2].doc);
}

TEST(TopScoreDocCollectorTest, SearchAfterWithDifferentSegments) {
    // Simulate multi-segment search with pagination
    // Segment 1: docs 0-99 (docBase=0)
    // Segment 2: docs 100-199 (docBase=100)

    // First search
    auto collector1 = TopScoreDocCollector::create(5);

    // Segment 1
    auto context1 = createContext(0);
    LeafCollector* leaf1 = collector1->getLeafCollector(context1);
    MockScorable scorer;
    leaf1->setScorer(&scorer);

    scorer.setDoc(10, 2.0f);
    leaf1->collect(10);
    scorer.setDoc(20, 1.5f);
    leaf1->collect(20);

    // Segment 2
    auto context2 = createContext(100);
    LeafCollector* leaf2 = collector1->getLeafCollector(context2);
    leaf2->setScorer(&scorer);

    scorer.setDoc(10, 3.0f);  // Global doc 110
    leaf2->collect(10);
    scorer.setDoc(20, 2.5f);  // Global doc 120
    leaf2->collect(20);

    TopDocs results1 = collector1->topDocs();
    ASSERT_EQ(4, results1.scoreDocs.size());

    // Check global doc IDs
    EXPECT_EQ(110, results1.scoreDocs[0].doc);  // score 3.0
    EXPECT_FLOAT_EQ(3.0f, results1.scoreDocs[0].score);
    EXPECT_EQ(120, results1.scoreDocs[1].doc);  // score 2.5
    EXPECT_EQ(10, results1.scoreDocs[2].doc);   // score 2.0
    EXPECT_EQ(20, results1.scoreDocs[3].doc);   // score 1.5
}

// ==================== Edge Cases ====================

TEST(TopScoreDocCollectorTest, NaNScoresIgnored) {
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect docs with NaN scores (should be ignored)
    scorer.setDoc(0, std::numeric_limits<float>::quiet_NaN());
    leaf->collect(0);

    scorer.setDoc(1, 1.0f);
    leaf->collect(1);

    // Only valid score should be collected
    TopDocs results = collector->topDocs();
    EXPECT_EQ(2, results.totalHits.value);   // Both counted in total
    ASSERT_EQ(1, results.scoreDocs.size());  // But only valid one kept
    EXPECT_EQ(1, results.scoreDocs[0].doc);
}

TEST(TopScoreDocCollectorTest, InfiniteScoresIgnored) {
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect docs with infinite scores (should be ignored)
    scorer.setDoc(0, std::numeric_limits<float>::infinity());
    leaf->collect(0);

    scorer.setDoc(1, -std::numeric_limits<float>::infinity());
    leaf->collect(1);

    scorer.setDoc(2, 1.0f);
    leaf->collect(2);

    // Only valid score should be collected
    TopDocs results = collector->topDocs();
    ASSERT_EQ(1, results.scoreDocs.size());
    EXPECT_EQ(2, results.scoreDocs[0].doc);
}

TEST(TopScoreDocCollectorTest, TopDocsSlicing) {
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect 10 docs
    for (int i = 0; i < 10; i++) {
        scorer.setDoc(i, 10.0f - i);
        leaf->collect(i);
    }

    // Get slice: start=2, howMany=3
    TopDocs results = collector->topDocs(2, 3);
    EXPECT_EQ(10, results.totalHits.value);
    ASSERT_EQ(3, results.scoreDocs.size());

    // Should be docs 2, 3, 4
    EXPECT_EQ(2, results.scoreDocs[0].doc);
    EXPECT_EQ(3, results.scoreDocs[1].doc);
    EXPECT_EQ(4, results.scoreDocs[2].doc);
}

TEST(TopScoreDocCollectorTest, TopDocsSlicingPastEnd) {
    auto collector = TopScoreDocCollector::create(10);
    auto context = createContext(0);
    LeafCollector* leaf = collector->getLeafCollector(context);

    MockScorable scorer;
    leaf->setScorer(&scorer);

    // Collect 5 docs
    for (int i = 0; i < 5; i++) {
        scorer.setDoc(i, 5.0f - i);
        leaf->collect(i);
    }

    // Request slice beyond available results
    TopDocs results = collector->topDocs(3, 10);
    ASSERT_EQ(2, results.scoreDocs.size());  // Only 2 docs after index 3
    EXPECT_EQ(3, results.scoreDocs[0].doc);
    EXPECT_EQ(4, results.scoreDocs[1].doc);

    // Request slice completely past end
    TopDocs emptyResults = collector->topDocs(10, 5);
    EXPECT_TRUE(emptyResults.scoreDocs.empty());
}
