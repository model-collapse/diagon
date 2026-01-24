// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <gtest/gtest.h>

using namespace diagon::index;
using namespace diagon::document;

// ==================== DocumentsWriterPerThread Tests ====================

TEST(DocumentsWriterPerThreadTest, InitialState) {
    DocumentsWriterPerThread dwpt;

    EXPECT_EQ(dwpt.getNumDocsInRAM(), 0);
    EXPECT_GT(dwpt.bytesUsed(), 0);  // Has base overhead
    EXPECT_FALSE(dwpt.needsFlush());
}

TEST(DocumentsWriterPerThreadTest, AddSingleDocument) {
    DocumentsWriterPerThread dwpt;

    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello world", TextField::TYPE_STORED));

    bool needsFlush = dwpt.addDocument(doc);

    EXPECT_EQ(dwpt.getNumDocsInRAM(), 1);
    EXPECT_GT(dwpt.bytesUsed(), 0);
    EXPECT_FALSE(needsFlush);  // Default limit is 1000 docs
}

TEST(DocumentsWriterPerThreadTest, AddMultipleDocuments) {
    DocumentsWriterPerThread dwpt;

    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    EXPECT_EQ(dwpt.getNumDocsInRAM(), 10);
    EXPECT_GT(dwpt.bytesUsed(), 0);
}

TEST(DocumentsWriterPerThreadTest, FlushByDocumentCount) {
    DocumentsWriterPerThread::Config config;
    config.maxBufferedDocs = 5;  // Flush after 5 docs
    DocumentsWriterPerThread dwpt(config);

    bool needsFlush = false;

    // Add 4 documents - should not trigger flush
    for (int i = 0; i < 4; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        needsFlush = dwpt.addDocument(doc);
    }

    EXPECT_FALSE(needsFlush);
    EXPECT_EQ(dwpt.getNumDocsInRAM(), 4);

    // Add 5th document - should trigger flush
    Document doc5;
    doc5.add(std::make_unique<TextField>("body", "doc5", TextField::TYPE_STORED));
    needsFlush = dwpt.addDocument(doc5);

    EXPECT_TRUE(needsFlush);
    EXPECT_EQ(dwpt.getNumDocsInRAM(), 5);
}

TEST(DocumentsWriterPerThreadTest, FlushByRAMLimit) {
    DocumentsWriterPerThread::Config config;
    config.ramBufferSizeMB = 1;  // Small RAM limit (1MB)
    config.maxBufferedDocs = 10000;  // High doc limit
    DocumentsWriterPerThread dwpt(config);

    // Add documents with many unique terms to increase RAM usage
    bool needsFlush = false;
    int docsAdded = 0;

    for (int i = 0; i < 1000 && !needsFlush; i++) {
        Document doc;
        // Create document with many unique terms
        std::string text;
        for (int j = 0; j < 1000; j++) {
            text += "term_" + std::to_string(i) + "_" + std::to_string(j) + " ";
        }
        doc.add(std::make_unique<TextField>("body", text, TextField::TYPE_STORED));
        needsFlush = dwpt.addDocument(doc);
        docsAdded++;
    }

    // Should have triggered flush before hitting doc limit
    EXPECT_TRUE(needsFlush);
    EXPECT_LT(docsAdded, 10000);
    EXPECT_GT(dwpt.bytesUsed(), 0);  // Has accumulated RAM
}

TEST(DocumentsWriterPerThreadTest, Flush) {
    DocumentsWriterPerThread dwpt;

    // Add some documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    EXPECT_EQ(dwpt.getNumDocsInRAM(), 5);

    // Flush
    auto segmentInfo = dwpt.flush();

    // Should return SegmentInfo
    ASSERT_NE(segmentInfo, nullptr);
    EXPECT_FALSE(segmentInfo->name().empty());
    EXPECT_EQ(segmentInfo->name()[0], '_');  // Format: _0, _1, etc.
    EXPECT_EQ(segmentInfo->maxDoc(), 5);

    // Should be reset after flush
    EXPECT_EQ(dwpt.getNumDocsInRAM(), 0);
}

TEST(DocumentsWriterPerThreadTest, FlushEmptyBuffer) {
    DocumentsWriterPerThread dwpt;

    // Flush without adding documents
    auto segmentInfo = dwpt.flush();

    // Should return nullptr
    EXPECT_EQ(segmentInfo, nullptr);
    EXPECT_EQ(dwpt.getNumDocsInRAM(), 0);
}

TEST(DocumentsWriterPerThreadTest, MultipleFlushes) {
    DocumentsWriterPerThread dwpt;

    // First flush
    for (int i = 0; i < 3; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    auto segment1 = dwpt.flush();
    ASSERT_NE(segment1, nullptr);
    EXPECT_FALSE(segment1->name().empty());
    EXPECT_EQ(dwpt.getNumDocsInRAM(), 0);

    // Second flush
    for (int i = 0; i < 3; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    auto segment2 = dwpt.flush();
    ASSERT_NE(segment2, nullptr);
    EXPECT_FALSE(segment2->name().empty());
    EXPECT_NE(segment1, segment2);  // Different segment names
}

TEST(DocumentsWriterPerThreadTest, Reset) {
    DocumentsWriterPerThread dwpt;

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    EXPECT_EQ(dwpt.getNumDocsInRAM(), 5);
    EXPECT_GT(dwpt.bytesUsed(), 0);

    // Reset
    dwpt.reset();

    EXPECT_EQ(dwpt.getNumDocsInRAM(), 0);
    EXPECT_GT(dwpt.bytesUsed(), 0);  // Still has base overhead
}

TEST(DocumentsWriterPerThreadTest, FieldMetadataTracking) {
    DocumentsWriterPerThread dwpt;

    // Add document with multiple fields
    Document doc;
    doc.add(std::make_unique<TextField>("title", "test", TextField::TYPE_STORED));
    doc.add(std::make_unique<TextField>("body", "content", TextField::TYPE_STORED));
    doc.add(std::make_unique<NumericDocValuesField>("price", 100));

    dwpt.addDocument(doc);

    // Check field metadata tracked
    const auto& builder = dwpt.getFieldInfosBuilder();
    EXPECT_EQ(builder.getFieldCount(), 3);
    EXPECT_NE(builder.getFieldNumber("title"), -1);
    EXPECT_NE(builder.getFieldNumber("body"), -1);
    EXPECT_NE(builder.getFieldNumber("price"), -1);
}

TEST(DocumentsWriterPerThreadTest, PostingListsBuilt) {
    DocumentsWriterPerThread dwpt;

    // Add documents
    Document doc1;
    doc1.add(std::make_unique<TextField>("body", "hello world", TextField::TYPE_STORED));
    dwpt.addDocument(doc1);

    Document doc2;
    doc2.add(std::make_unique<TextField>("body", "hello there", TextField::TYPE_STORED));
    dwpt.addDocument(doc2);

    // Check posting lists built
    const auto& termsWriter = dwpt.getTermsWriter();
    auto terms = termsWriter.getTerms();

    EXPECT_EQ(terms.size(), 3);  // "hello", "there", "world"

    // "hello" appears in both docs
    auto helloPostings = termsWriter.getPostingList("hello");
    EXPECT_EQ(helloPostings.size(), 4);  // [0, 1, 1, 1] = [docID0, freq, docID1, freq]
}

TEST(DocumentsWriterPerThreadTest, BytesUsedIncreases) {
    DocumentsWriterPerThread dwpt;

    int64_t initialBytes = dwpt.bytesUsed();

    // Add documents - bytes should increase
    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "document content here", TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    int64_t afterBytes = dwpt.bytesUsed();
    EXPECT_GT(afterBytes, initialBytes);
}

TEST(DocumentsWriterPerThreadTest, ConfigurationRespected) {
    DocumentsWriterPerThread::Config config;
    config.maxBufferedDocs = 10;
    config.ramBufferSizeMB = 32;

    DocumentsWriterPerThread dwpt(config);

    // Add 9 documents - should not trigger flush
    for (int i = 0; i < 9; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
        bool needsFlush = dwpt.addDocument(doc);
        EXPECT_FALSE(needsFlush);
    }

    // Add 10th document - should trigger flush
    Document doc10;
    doc10.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
    bool needsFlush = dwpt.addDocument(doc10);
    EXPECT_TRUE(needsFlush);
}

TEST(DocumentsWriterPerThreadTest, SegmentNumberIncreases) {
    DocumentsWriterPerThread dwpt1;
    DocumentsWriterPerThread dwpt2;

    // Add docs to dwpt1 and flush
    Document doc1;
    doc1.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
    dwpt1.addDocument(doc1);
    auto segment1 = dwpt1.flush();

    // Add docs to dwpt2 and flush
    Document doc2;
    doc2.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
    dwpt2.addDocument(doc2);
    auto segment2 = dwpt2.flush();

    // Segment names should be different
    ASSERT_NE(segment1, nullptr);
    ASSERT_NE(segment2, nullptr);
    EXPECT_NE(segment1->name(), segment2->name());

    // Both should start with underscore
    EXPECT_EQ(segment1->name()[0], '_');
    EXPECT_EQ(segment2->name()[0], '_');
}
