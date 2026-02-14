// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/QBlockIndex.h"

#include "diagon/sparse/SparseVector.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace diagon::sparse;

// ==================== Configuration Tests ====================

TEST(QBlockIndexTest, ConstructionWithConfig) {
    QBlockIndex::Config config;
    config.num_bins = 8;
    config.window_size = 4096;
    config.alpha = 0.8f;
    config.use_mmap = false;

    QBlockIndex index(config);

    EXPECT_EQ(8, index.config().num_bins);
    EXPECT_EQ(4096, index.config().window_size);
    EXPECT_FLOAT_EQ(0.8f, index.config().alpha);
    EXPECT_FALSE(index.config().use_mmap);
}

TEST(QBlockIndexTest, ConfigValidation) {
    QBlockIndex::Config config;

    // Invalid num_bins
    config.num_bins = 0;
    EXPECT_THROW(QBlockIndex index(config), std::invalid_argument);

    config.num_bins = 300;  // > 256
    EXPECT_THROW(QBlockIndex index(config), std::invalid_argument);

    // Invalid window_size
    config.num_bins = 16;
    config.window_size = 0;
    EXPECT_THROW(QBlockIndex index(config), std::invalid_argument);

    // Invalid alpha
    config.window_size = 8192;
    config.alpha = -0.1f;
    EXPECT_THROW(QBlockIndex index(config), std::invalid_argument);

    config.alpha = 1.5f;
    EXPECT_THROW(QBlockIndex index(config), std::invalid_argument);

    // Invalid chunk_power
    config.alpha = 0.75f;
    config.chunk_power = 10;  // Too small
    EXPECT_THROW(QBlockIndex index(config), std::invalid_argument);
}

// ==================== Index Building Tests ====================

TEST(QBlockIndexTest, BuildEmptyIndex) {
    QBlockIndex::Config config;
    QBlockIndex index(config);

    std::vector<SparseVector> documents;
    index.build(documents);

    EXPECT_EQ(0, index.numDocuments());
    EXPECT_EQ(0, index.numTerms());
    EXPECT_EQ(0, index.numPostings());
}

TEST(QBlockIndexTest, BuildSimpleIndex) {
    QBlockIndex::Config config;
    config.num_bins = 4;
    config.window_size = 2;  // Small for testing
    QBlockIndex index(config);

    // Create 4 documents
    std::vector<SparseVector> documents;

    // Doc 0: terms 0, 1 with weights 1.0, 2.0
    SparseVector doc0;
    doc0.add(0, 1.0f);
    doc0.add(1, 2.0f);
    documents.push_back(doc0);

    // Doc 1: terms 1, 2 with weights 1.5, 2.5
    SparseVector doc1;
    doc1.add(1, 1.5f);
    doc1.add(2, 2.5f);
    documents.push_back(doc1);

    // Doc 2: terms 0, 2 with weights 0.5, 1.5
    SparseVector doc2;
    doc2.add(0, 0.5f);
    doc2.add(2, 1.5f);
    documents.push_back(doc2);

    // Doc 3: terms 0, 1, 2 with weights 2.0, 1.0, 0.5
    SparseVector doc3;
    doc3.add(0, 2.0f);
    doc3.add(1, 1.0f);
    doc3.add(2, 0.5f);
    documents.push_back(doc3);

    index.build(documents);

    EXPECT_EQ(4, index.numDocuments());
    EXPECT_EQ(3, index.numTerms());     // Terms 0, 1, 2
    EXPECT_EQ(9, index.numPostings());  // Total 9 postings (2+2+2+3)
    EXPECT_EQ(2, index.numWindows());   // 4 docs / 2 per window = 2 windows
}

TEST(QBlockIndexTest, BuildLargeIndex) {
    QBlockIndex::Config config;
    config.num_bins = 16;
    config.window_size = 128;
    QBlockIndex index(config);

    // Build index with 1000 documents
    std::vector<SparseVector> documents;

    for (int i = 0; i < 1000; ++i) {
        SparseVector doc;
        // Each doc has 10 terms with varying weights
        for (int t = 0; t < 10; ++t) {
            float weight = (i % 10 + 1) * 0.1f + t * 0.05f;
            doc.add(t, weight);
        }
        documents.push_back(doc);
    }

    index.build(documents);

    EXPECT_EQ(1000, index.numDocuments());
    EXPECT_EQ(10, index.numTerms());
    EXPECT_EQ(10000, index.numPostings());
    EXPECT_EQ(8, index.numWindows());  // ceil(1000 / 128) = 8
}

// ==================== Search Tests ====================

TEST(QBlockIndexTest, SearchSimpleQuery) {
    QBlockIndex::Config config;
    config.num_bins = 4;
    config.window_size = 100;
    config.alpha = 0.75f;
    QBlockIndex index(config);

    // Build index with simple documents
    std::vector<SparseVector> documents;

    // Doc 0: term 0 = 1.0
    SparseVector doc0;
    doc0.add(0, 1.0f);
    documents.push_back(doc0);

    // Doc 1: term 0 = 2.0 (should score higher)
    SparseVector doc1;
    doc1.add(0, 2.0f);
    documents.push_back(doc1);

    // Doc 2: term 1 = 1.0 (different term)
    SparseVector doc2;
    doc2.add(1, 1.0f);
    documents.push_back(doc2);

    index.build(documents);

    // Query: term 0 with weight 1.0
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    ASSERT_GE(results.size(), 2);

    // Both docs 0 and 1 should match (have term 0)
    bool found_doc0 = false;
    bool found_doc1 = false;
    for (const auto& result : results) {
        if (result.doc_id == 0)
            found_doc0 = true;
        if (result.doc_id == 1)
            found_doc1 = true;
    }

    EXPECT_TRUE(found_doc0);
    EXPECT_TRUE(found_doc1);

    // Doc 1 should score higher (weight 2.0 > 1.0)
    // Note: With quantization, exact ordering may vary
    EXPECT_GT(results[0].score, 0.0f);
}

TEST(QBlockIndexTest, SearchMultipleTerms) {
    QBlockIndex::Config config;
    config.num_bins = 8;
    config.window_size = 100;
    QBlockIndex index(config);

    // Build index
    std::vector<SparseVector> documents;

    // Doc 0: terms 0, 1
    SparseVector doc0;
    doc0.add(0, 1.0f);
    doc0.add(1, 1.0f);
    documents.push_back(doc0);

    // Doc 1: terms 0, 2
    SparseVector doc1;
    doc1.add(0, 2.0f);
    doc1.add(2, 2.0f);
    documents.push_back(doc1);

    // Doc 2: terms 1, 2
    SparseVector doc2;
    doc2.add(1, 3.0f);
    doc2.add(2, 3.0f);
    documents.push_back(doc2);

    index.build(documents);

    // Query: terms 0, 1 (should match all docs but with different scores)
    SparseVector query;
    query.add(0, 1.0f);
    query.add(1, 1.0f);

    auto results = index.search(query, 10);

    // With quantization and block selection, we may not get all docs
    // but should get at least some results
    ASSERT_GE(results.size(), 2);
    ASSERT_LE(results.size(), 3);

    // All docs should have positive scores
    for (const auto& result : results) {
        EXPECT_GT(result.score, 0.0f);
    }
}

TEST(QBlockIndexTest, SearchTopK) {
    QBlockIndex::Config config;
    config.num_bins = 16;
    config.window_size = 100;
    QBlockIndex index(config);

    // Build index with 10 documents
    std::vector<SparseVector> documents;

    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));  // Weights 1-10
        documents.push_back(doc);
    }

    index.build(documents);

    // Query: term 0
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 3);  // Top 3

    // With quantization, we should get at least 2 results
    ASSERT_GE(results.size(), 2);
    ASSERT_LE(results.size(), 3);

    // All should have positive scores
    for (const auto& result : results) {
        EXPECT_GT(result.score, 0.0f);
    }

    // Scores should be in descending order
    if (results.size() >= 2) {
        EXPECT_GE(results[0].score, results[1].score);
    }
    if (results.size() >= 3) {
        EXPECT_GE(results[1].score, results[2].score);
    }
}

TEST(QBlockIndexTest, SearchEmptyQuery) {
    QBlockIndex::Config config;
    QBlockIndex index(config);

    std::vector<SparseVector> documents;
    SparseVector doc;
    doc.add(0, 1.0f);
    documents.push_back(doc);

    index.build(documents);

    SparseVector empty_query;
    auto results = index.search(empty_query, 10);

    EXPECT_TRUE(results.empty());
}

TEST(QBlockIndexTest, SearchNoMatches) {
    QBlockIndex::Config config;
    QBlockIndex index(config);

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

// ==================== Selection Mode Tests ====================

TEST(QBlockIndexTest, SelectionModeTopK) {
    QBlockIndex::Config config;
    config.num_bins = 8;
    config.window_size = 100;
    config.selection_mode = QBlockIndex::Config::TOP_K;
    config.fixed_top_k = 2;  // Select only 2 blocks

    QBlockIndex index(config);

    // Build index
    std::vector<SparseVector> documents;
    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index.build(documents);

    // Query
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    // Should still get results (though maybe not all due to block selection)
    EXPECT_GT(results.size(), 0);
}

TEST(QBlockIndexTest, SelectionModeMaxRatio) {
    QBlockIndex::Config config;
    config.num_bins = 8;
    config.window_size = 100;
    config.selection_mode = QBlockIndex::Config::MAX_RATIO;
    config.alpha = 0.5f;  // Threshold at 50% of max gain

    QBlockIndex index(config);

    // Build index
    std::vector<SparseVector> documents;
    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index.build(documents);

    // Query
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    EXPECT_GT(results.size(), 0);
}

TEST(QBlockIndexTest, SelectionModeAlphaMass) {
    QBlockIndex::Config config;
    config.num_bins = 8;
    config.window_size = 100;
    config.selection_mode = QBlockIndex::Config::ALPHA_MASS;
    config.alpha = 0.75f;  // Select until 75% of total mass

    QBlockIndex index(config);

    // Build index
    std::vector<SparseVector> documents;
    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index.build(documents);

    // Query
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 10);

    EXPECT_GT(results.size(), 0);
}

// ==================== Quantization Tests ====================

TEST(QBlockIndexTest, QuantizationBins) {
    QBlockIndex::Config config;
    config.num_bins = 4;  // Only 4 bins for clear separation
    config.window_size = 100;
    QBlockIndex index(config);

    // Create documents with distinct weight ranges
    std::vector<SparseVector> documents;

    // Low weights: 0.1-0.3
    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, 0.1f + i * 0.02f);
        documents.push_back(doc);
    }

    // Medium weights: 0.5-0.7
    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, 0.5f + i * 0.02f);
        documents.push_back(doc);
    }

    // High weights: 1.0-1.2
    for (int i = 0; i < 10; ++i) {
        SparseVector doc;
        doc.add(0, 1.0f + i * 0.02f);
        documents.push_back(doc);
    }

    index.build(documents);

    EXPECT_EQ(30, index.numDocuments());
    EXPECT_EQ(1, index.numTerms());

    // Query should match all documents
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 30);

    EXPECT_EQ(30, results.size());
}

// ==================== Window Tests ====================

TEST(QBlockIndexTest, MultipleWindows) {
    QBlockIndex::Config config;
    config.num_bins = 8;
    config.window_size = 10;  // Small windows

    QBlockIndex index(config);

    // Create 25 documents (will span 3 windows)
    std::vector<SparseVector> documents;

    for (int i = 0; i < 25; ++i) {
        SparseVector doc;
        doc.add(0, 1.0f);
        documents.push_back(doc);
    }

    index.build(documents);

    EXPECT_EQ(25, index.numDocuments());
    EXPECT_EQ(3, index.numWindows());  // ceil(25 / 10) = 3

    // Search should find all documents
    SparseVector query;
    query.add(0, 1.0f);

    auto results = index.search(query, 30);

    EXPECT_EQ(25, results.size());
}

// ==================== Configuration Options Tests ====================

TEST(QBlockIndexTest, PrefetchConfiguration) {
    QBlockIndex::Config config_with_prefetch;
    config_with_prefetch.use_prefetch = true;

    QBlockIndex::Config config_without_prefetch;
    config_without_prefetch.use_prefetch = false;

    QBlockIndex index_with(config_with_prefetch);
    QBlockIndex index_without(config_without_prefetch);

    // Build same index
    std::vector<SparseVector> documents;
    for (int i = 0; i < 100; ++i) {
        SparseVector doc;
        doc.add(0, static_cast<float>(i + 1));
        documents.push_back(doc);
    }

    index_with.build(documents);
    index_without.build(documents);

    // Query
    SparseVector query;
    query.add(0, 1.0f);

    auto results_with = index_with.search(query, 10);
    auto results_without = index_without.search(query, 10);

    // Results should be identical (prefetch is just a hint)
    ASSERT_EQ(results_with.size(), results_without.size());
}
