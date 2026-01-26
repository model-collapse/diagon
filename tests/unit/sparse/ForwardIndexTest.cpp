// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/QBlockIndex.h"
#include "diagon/sparse/SindiIndex.h"
#include "diagon/sparse/SparseVector.h"

#include <gtest/gtest.h>

using namespace diagon::sparse;

// ==================== Test Fixtures ====================

class SindiForwardIndexTest : public ::testing::Test {
protected:
    SindiIndex::Config config;

    void SetUp() override {
        config.block_size = 128;
        config.use_block_max = true;
        config.use_simd = true;
        config.use_mmap = false;  // In-memory for testing
        config.use_prefetch = true;
    }

    std::vector<SparseVector> createTestDocuments() {
        std::vector<SparseVector> docs;

        // Doc 0: {0: 1.0, 1: 2.0, 2: 3.0}
        SparseVector doc0;
        doc0.add(0, 1.0f);
        doc0.add(1, 2.0f);
        doc0.add(2, 3.0f);
        docs.push_back(doc0);

        // Doc 1: {1: 0.5, 3: 1.5}
        SparseVector doc1;
        doc1.add(1, 0.5f);
        doc1.add(3, 1.5f);
        docs.push_back(doc1);

        // Doc 2: {0: 2.5, 2: 1.0, 4: 0.8}
        SparseVector doc2;
        doc2.add(0, 2.5f);
        doc2.add(2, 1.0f);
        doc2.add(4, 0.8f);
        docs.push_back(doc2);

        // Doc 3: Empty document
        SparseVector doc3;
        docs.push_back(doc3);

        // Doc 4: {5: 3.0}
        SparseVector doc4;
        doc4.add(5, 3.0f);
        docs.push_back(doc4);

        return docs;
    }
};

class QBlockForwardIndexTest : public ::testing::Test {
protected:
    QBlockIndex::Config config;

    void SetUp() override {
        config.num_bins = 16;
        config.window_size = 8192;
        config.alpha = 0.75f;
        config.selection_mode = QBlockIndex::Config::ALPHA_MASS;
        config.use_mmap = false;  // In-memory for testing
        config.use_prefetch = true;
    }

    std::vector<SparseVector> createTestDocuments() {
        std::vector<SparseVector> docs;

        // Doc 0: {0: 1.0, 1: 2.0, 2: 3.0}
        SparseVector doc0;
        doc0.add(0, 1.0f);
        doc0.add(1, 2.0f);
        doc0.add(2, 3.0f);
        docs.push_back(doc0);

        // Doc 1: {1: 0.5, 3: 1.5}
        SparseVector doc1;
        doc1.add(1, 0.5f);
        doc1.add(3, 1.5f);
        docs.push_back(doc1);

        // Doc 2: {0: 2.5, 2: 1.0, 4: 0.8}
        SparseVector doc2;
        doc2.add(0, 2.5f);
        doc2.add(2, 1.0f);
        doc2.add(4, 0.8f);
        docs.push_back(doc2);

        // Doc 3: Empty document
        SparseVector doc3;
        docs.push_back(doc3);

        // Doc 4: {5: 3.0}
        SparseVector doc4;
        doc4.add(5, 3.0f);
        docs.push_back(doc4);

        return docs;
    }
};

// ==================== SINDI Forward Index Tests ====================

TEST_F(SindiForwardIndexTest, ForwardIndexBuiltDuringBuild) {
    SindiIndex index(config);
    auto docs = createTestDocuments();

    EXPECT_FALSE(index.hasForwardIndex());

    index.build(docs);

    EXPECT_TRUE(index.hasForwardIndex());
}

TEST_F(SindiForwardIndexTest, GetDocumentReturnsCorrectVector) {
    SindiIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Check Doc 0: {0: 1.0, 1: 2.0, 2: 3.0}
    auto retrieved_doc0 = index.getDocument(0);
    ASSERT_EQ(3, retrieved_doc0.size());
    EXPECT_EQ(0, retrieved_doc0[0].index);
    EXPECT_FLOAT_EQ(1.0f, retrieved_doc0[0].value);
    EXPECT_EQ(1, retrieved_doc0[1].index);
    EXPECT_FLOAT_EQ(2.0f, retrieved_doc0[1].value);
    EXPECT_EQ(2, retrieved_doc0[2].index);
    EXPECT_FLOAT_EQ(3.0f, retrieved_doc0[2].value);

    // Check Doc 1: {1: 0.5, 3: 1.5}
    auto retrieved_doc1 = index.getDocument(1);
    ASSERT_EQ(2, retrieved_doc1.size());
    EXPECT_EQ(1, retrieved_doc1[0].index);
    EXPECT_FLOAT_EQ(0.5f, retrieved_doc1[0].value);
    EXPECT_EQ(3, retrieved_doc1[1].index);
    EXPECT_FLOAT_EQ(1.5f, retrieved_doc1[1].value);
}

TEST_F(SindiForwardIndexTest, GetEmptyDocument) {
    SindiIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Doc 3 is empty
    auto retrieved_doc3 = index.getDocument(3);
    EXPECT_EQ(0, retrieved_doc3.size());
}

TEST_F(SindiForwardIndexTest, GetDocumentOutOfRange) {
    SindiIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    EXPECT_THROW(index.getDocument(5), std::out_of_range);
    EXPECT_THROW(index.getDocument(100), std::out_of_range);
}

TEST_F(SindiForwardIndexTest, GetDocumentBeforeBuild) {
    SindiIndex index(config);

    EXPECT_THROW(index.getDocument(0), std::out_of_range);
}

TEST_F(SindiForwardIndexTest, PrefetchDocumentNoThrow) {
    SindiIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Prefetch should not throw
    EXPECT_NO_THROW(index.prefetchDocument(0));
    EXPECT_NO_THROW(index.prefetchDocument(4));

    // Prefetch out of range should not throw (silently ignored)
    EXPECT_NO_THROW(index.prefetchDocument(100));
}

TEST_F(SindiForwardIndexTest, GetAllDocumentsMatchOriginal) {
    SindiIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Verify all documents match original
    for (size_t i = 0; i < docs.size(); ++i) {
        auto retrieved = index.getDocument(static_cast<uint32_t>(i));
        const auto& original = docs[i];

        ASSERT_EQ(original.size(), retrieved.size()) << "Mismatch at doc " << i;

        for (size_t j = 0; j < original.size(); ++j) {
            EXPECT_EQ(original[j].index, retrieved[j].index)
                << "Index mismatch at doc " << i << ", position " << j;
            EXPECT_FLOAT_EQ(original[j].value, retrieved[j].value)
                << "Value mismatch at doc " << i << ", position " << j;
        }
    }
}

TEST_F(SindiForwardIndexTest, GetDocumentWithPrefetch) {
    SindiIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Prefetch documents before retrieving
    for (uint32_t i = 0; i < docs.size(); ++i) {
        index.prefetchDocument(i);
    }

    // Retrieve and verify
    for (uint32_t i = 0; i < docs.size(); ++i) {
        auto retrieved = index.getDocument(i);
        EXPECT_EQ(docs[i].size(), retrieved.size());
    }
}

// ==================== QBlock Forward Index Tests ====================

TEST_F(QBlockForwardIndexTest, ForwardIndexBuiltDuringBuild) {
    QBlockIndex index(config);
    auto docs = createTestDocuments();

    EXPECT_FALSE(index.hasForwardIndex());

    index.build(docs);

    EXPECT_TRUE(index.hasForwardIndex());
}

TEST_F(QBlockForwardIndexTest, GetDocumentReturnsCorrectVector) {
    QBlockIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Check Doc 0: {0: 1.0, 1: 2.0, 2: 3.0}
    auto retrieved_doc0 = index.getDocument(0);
    ASSERT_EQ(3, retrieved_doc0.size());
    EXPECT_EQ(0, retrieved_doc0[0].index);
    EXPECT_FLOAT_EQ(1.0f, retrieved_doc0[0].value);
    EXPECT_EQ(1, retrieved_doc0[1].index);
    EXPECT_FLOAT_EQ(2.0f, retrieved_doc0[1].value);
    EXPECT_EQ(2, retrieved_doc0[2].index);
    EXPECT_FLOAT_EQ(3.0f, retrieved_doc0[2].value);

    // Check Doc 2: {0: 2.5, 2: 1.0, 4: 0.8}
    auto retrieved_doc2 = index.getDocument(2);
    ASSERT_EQ(3, retrieved_doc2.size());
    EXPECT_EQ(0, retrieved_doc2[0].index);
    EXPECT_FLOAT_EQ(2.5f, retrieved_doc2[0].value);
    EXPECT_EQ(2, retrieved_doc2[1].index);
    EXPECT_FLOAT_EQ(1.0f, retrieved_doc2[1].value);
    EXPECT_EQ(4, retrieved_doc2[2].index);
    EXPECT_FLOAT_EQ(0.8f, retrieved_doc2[2].value);
}

TEST_F(QBlockForwardIndexTest, GetEmptyDocument) {
    QBlockIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Doc 3 is empty
    auto retrieved_doc3 = index.getDocument(3);
    EXPECT_EQ(0, retrieved_doc3.size());
}

TEST_F(QBlockForwardIndexTest, GetDocumentOutOfRange) {
    QBlockIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    EXPECT_THROW(index.getDocument(5), std::out_of_range);
    EXPECT_THROW(index.getDocument(100), std::out_of_range);
}

TEST_F(QBlockForwardIndexTest, GetDocumentBeforeBuild) {
    QBlockIndex index(config);

    EXPECT_THROW(index.getDocument(0), std::out_of_range);
}

TEST_F(QBlockForwardIndexTest, GetAllDocumentsMatchOriginal) {
    QBlockIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Verify all documents match original
    for (size_t i = 0; i < docs.size(); ++i) {
        auto retrieved = index.getDocument(static_cast<uint32_t>(i));
        const auto& original = docs[i];

        ASSERT_EQ(original.size(), retrieved.size()) << "Mismatch at doc " << i;

        for (size_t j = 0; j < original.size(); ++j) {
            EXPECT_EQ(original[j].index, retrieved[j].index)
                << "Index mismatch at doc " << i << ", position " << j;
            EXPECT_FLOAT_EQ(original[j].value, retrieved[j].value)
                << "Value mismatch at doc " << i << ", position " << j;
        }
    }
}

TEST_F(QBlockForwardIndexTest, PrefetchDocumentNoThrow) {
    QBlockIndex index(config);
    auto docs = createTestDocuments();
    index.build(docs);

    // Prefetch should not throw
    EXPECT_NO_THROW(index.prefetchDocument(0));
    EXPECT_NO_THROW(index.prefetchDocument(4));

    // Prefetch out of range should not throw (silently ignored)
    EXPECT_NO_THROW(index.prefetchDocument(100));
}

TEST_F(QBlockForwardIndexTest, LargeDocumentCollection) {
    QBlockIndex index(config);
    std::vector<SparseVector> docs;

    // Create 1000 documents with varying sparsity
    for (uint32_t i = 0; i < 1000; ++i) {
        SparseVector doc;
        // Each document has 5-10 terms
        uint32_t num_terms = 5 + (i % 6);
        for (uint32_t j = 0; j < num_terms; ++j) {
            doc.add((i * 10 + j) % 100, static_cast<float>(j + 1) * 0.1f);
        }
        docs.push_back(doc);
    }

    index.build(docs);

    // Verify random documents
    for (uint32_t i : {0, 50, 100, 500, 999}) {
        auto retrieved = index.getDocument(i);
        EXPECT_EQ(docs[i].size(), retrieved.size()) << "Size mismatch at doc " << i;

        for (size_t j = 0; j < docs[i].size(); ++j) {
            EXPECT_EQ(docs[i][j].index, retrieved[j].index);
            EXPECT_FLOAT_EQ(docs[i][j].value, retrieved[j].value);
        }
    }
}
