// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/SindiIndex.h"
#include "diagon/sparse/SparseVector.h"

#include <gtest/gtest.h>
#include <algorithm>

using namespace diagon::sparse;

// ==================== Index Building Tests ====================

TEST(SindiIndexTest, ConstructionWithConfig) {
    SindiIndex::Config config;
    config.block_size = 64;
    config.use_simd = true;
    config.use_mmap = false;

    SindiIndex index(config);

    EXPECT_EQ(64, index.config().block_size);
    EXPECT_TRUE(index.config().use_simd);
    EXPECT_FALSE(index.config().use_mmap);
}

TEST(SindiIndexTest, BuildEmptyIndex) {
    SindiIndex::Config config;
    SindiIndex index(config);

    std::vector<SparseVector> documents;
    index.build(documents);

    EXPECT_EQ(0, index.numDocuments());
    EXPECT_EQ(0, index.numTerms());
    EXPECT_EQ(0, index.numPostings());
}

TEST(SindiIndexTest, BuildSimpleIndex) {
    SindiIndex::Config config;
    config.block_size = 128;
    SindiIndex index(config);

    // Create 3 documents
    std::vector<SparseVector> documents;

    // Doc 0: terms 0, 1, 2
    SparseVector doc0;
    doc0.add(0, 1.0f);
    doc0.add(1, 2.0f);
    doc0.add(2, 3.0f);
    documents.push_back(doc0);

    // Doc 1: terms 1, 2, 3
    SparseVector doc1;
    doc1.add(1, 1.5f);
    doc1.add(2, 2.5f);
    doc1.add(3, 3.5f);
    documents.push_back(doc1);

    // Doc 2: terms 0, 2, 4
    SparseVector doc2;
    doc2.add(0, 0.5f);
    doc2.add(2, 1.5f);
    doc2.add(4, 2.5f);
    documents.push_back(doc2);

    index.build(documents);

    EXPECT_EQ(3, index.numDocuments());
    EXPECT_EQ(5, index.numTerms());  // Terms 0-4
    EXPECT_EQ(9, index.numPostings());  // Total 9 postings
}

// ==================== Search Tests ====================

TEST(SindiIndexTest, SearchExactMatch) {
    SindiIndex::Config config;
    config.block_size = 128;
    config.use_block_max = false;  // Disable for simple test
    SindiIndex index(config);

    // Build index
    std::vector<SparseVector> documents;

    SparseVector doc0;
    doc0.add(0, 1.0f);
    doc0.add(1, 2.0f);
    documents.push_back(doc0);

    SparseVector doc1;
    doc1.add(0, 3.0f);
    doc1.add(1, 4.0f);
    documents.push_back(doc1);

    index.build(documents);

    // Query: term 0 with weight 1.0
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    ASSERT_EQ(2, results.size());

    // Doc 1 should score higher (3.0) than doc 0 (1.0)
    EXPECT_EQ(1, results[0].doc_id);
    EXPECT_FLOAT_EQ(3.0f, results[0].score);

    EXPECT_EQ(0, results[1].doc_id);
    EXPECT_FLOAT_EQ(1.0f, results[1].score);
}

TEST(SindiIndexTest, SearchMultipleTerms) {
    SindiIndex::Config config;
    config.use_block_max = false;
    SindiIndex index(config);

    // Build index
    std::vector<SparseVector> documents;

    // Doc 0: term 0=1.0, term 1=1.0
    SparseVector doc0;
    doc0.add(0, 1.0f);
    doc0.add(1, 1.0f);
    documents.push_back(doc0);

    // Doc 1: term 0=2.0, term 2=2.0
    SparseVector doc1;
    doc1.add(0, 2.0f);
    doc1.add(2, 2.0f);
    documents.push_back(doc1);

    // Doc 2: term 1=3.0, term 2=3.0
    SparseVector doc2;
    doc2.add(1, 3.0f);
    doc2.add(2, 3.0f);
    documents.push_back(doc2);

    index.build(documents);

    // Query: term 0=1.0, term 1=1.0
    SparseVector query;
    query.add(0, 1.0f);
    query.add(1, 1.0f);

    auto results = index.search(query, 10);

    ASSERT_EQ(3, results.size());

    // Doc 0: 1.0*1.0 + 1.0*1.0 = 2.0
    // Doc 1: 1.0*2.0 + 0 = 2.0
    // Doc 2: 0 + 1.0*3.0 = 3.0
    // Sorted: Doc 2 (3.0), Doc 0 (2.0), Doc 1 (2.0)

    EXPECT_EQ(2, results[0].doc_id);
    EXPECT_FLOAT_EQ(3.0f, results[0].score);

    // Docs 0 and 1 both score 2.0, order may vary
    EXPECT_FLOAT_EQ(2.0f, results[1].score);
    EXPECT_FLOAT_EQ(2.0f, results[2].score);
}

TEST(SindiIndexTest, SearchTopK) {
    SindiIndex::Config config;
    SindiIndex index(config);

    // Build index with 10 documents
    std::vector<SparseVector> documents;

    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));  // Scores 1-10
        documents.push_back(doc);
    }

    index.build(documents);

    // Query: term 0
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 3);  // Top 3

    ASSERT_EQ(3, results.size());

    // Top 3: docs 9, 8, 7 with scores 10, 9, 8
    EXPECT_EQ(9, results[0].doc_id);
    EXPECT_FLOAT_EQ(10.0f, results[0].score);

    EXPECT_EQ(8, results[1].doc_id);
    EXPECT_FLOAT_EQ(9.0f, results[1].score);

    EXPECT_EQ(7, results[2].doc_id);
    EXPECT_FLOAT_EQ(8.0f, results[2].score);
}

TEST(SindiIndexTest, SearchEmptyQuery) {
    SindiIndex::Config config;
    SindiIndex index(config);

    std::vector<SparseVector> documents;
    SparseVector doc;
    doc.add(0, 1.0f);
    documents.push_back(doc);

    index.build(documents);

    SparseVector empty_query;
    auto results = index.search(empty_query, 10);

    EXPECT_TRUE(results.empty());
}

TEST(SindiIndexTest, SearchNoMatches) {
    SindiIndex::Config config;
    SindiIndex index(config);

    // Build index with term 0
    std::vector<SparseVector> documents;
    SparseVector doc;
    doc.add(0, 1.0f);
    documents.push_back(doc);

    index.build(documents);

    // Query for term 1 (not in index)
    SparseVector query;
    query.add(1, 1.0f);

    auto results = index.search(query, 10);

    EXPECT_TRUE(results.empty());
}

// ==================== Block-Max WAND Tests ====================

TEST(SindiIndexTest, SearchWithWAND) {
    SindiIndex::Config config;
    config.block_size = 2;  // Small blocks for testing
    config.use_block_max = true;
    SindiIndex index(config);

    // Build index
    std::vector<SparseVector> documents;

    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index.build(documents);

    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 5);

    ASSERT_EQ(5, results.size());

    // Top 5: docs 9,8,7,6,5 with scores 10,9,8,7,6
    EXPECT_EQ(9, results[0].doc_id);
    EXPECT_FLOAT_EQ(10.0f, results[0].score);
}

// ==================== Configuration Tests ====================

TEST(SindiIndexTest, ConfigValidation) {
    SindiIndex::Config config;
    config.block_size = 0;  // Invalid

    EXPECT_THROW(SindiIndex index(config), std::invalid_argument);
}

TEST(SindiIndexTest, ConfigChunkPowerValidation) {
    SindiIndex::Config config;
    config.chunk_power = 10;  // Invalid (too small)

    EXPECT_THROW(SindiIndex index(config), std::invalid_argument);
}

// ==================== Large Index Tests ====================

TEST(SindiIndexTest, LargeIndex) {
    SindiIndex::Config config;
    config.block_size = 128;
    SindiIndex index(config);

    // Build index with 1000 documents
    std::vector<SparseVector> documents;

    for (int i = 0; i < 1000; ++i) {
        SparseVector doc;
        // Each doc has 10 terms
        for (int t = 0; t < 10; ++t) {
            doc.add(t, static_cast<float>(i % 10 + 1) * 0.1f);
        }
        documents.push_back(doc);
    }

    index.build(documents);

    EXPECT_EQ(1000, index.numDocuments());
    EXPECT_EQ(10, index.numTerms());
    EXPECT_EQ(10000, index.numPostings());

    // Search
    SparseVector query;
    query.add(0, 1.0f);
    query.add(5, 1.0f);

    auto results = index.search(query, 10);

    EXPECT_EQ(10, results.size());
    // All results should have positive scores
    for (const auto& result : results) {
        EXPECT_GT(result.score, 0.0f);
    }
}

// ==================== SIMD Configuration Tests ====================

TEST(SindiIndexTest, SearchWithSIMDEnabled) {
    if (!SindiScorer::hasAVX2()) {
        GTEST_SKIP() << "AVX2 not available";
    }

    SindiIndex::Config config;
    config.use_simd = true;
    SindiIndex index(config);

    std::vector<SparseVector> documents;
    for (int i = 0; i < 100; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index.build(documents);

    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    EXPECT_EQ(10, results.size());
}

TEST(SindiIndexTest, SearchWithSIMDDisabled) {
    SindiIndex::Config config;
    config.use_simd = false;
    SindiIndex index(config);

    std::vector<SparseVector> documents;
    for (int i = 0; i < 100; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index.build(documents);

    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    EXPECT_EQ(10, results.size());
}
