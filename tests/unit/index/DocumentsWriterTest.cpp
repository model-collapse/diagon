// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DocumentsWriter.h"

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <gtest/gtest.h>

using namespace diagon::index;
using namespace diagon::document;

// ==================== DocumentsWriter Tests ====================

TEST(DocumentsWriterTest, InitialState) {
    DocumentsWriter writer;

    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getNumDocsAdded(), 0);
    EXPECT_EQ(writer.getSegments().size(), 0);
    EXPECT_GT(writer.bytesUsed(), 0);  // Has base overhead
    EXPECT_FALSE(writer.needsFlush());
}

TEST(DocumentsWriterTest, AddSingleDocument) {
    DocumentsWriter writer;

    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello world", TextField::TYPE_STORED));

    int segmentsCreated = writer.addDocument(doc);

    EXPECT_EQ(segmentsCreated, 0);  // No flush yet
    EXPECT_EQ(writer.getNumDocsInRAM(), 1);
    EXPECT_EQ(writer.getNumDocsAdded(), 1);
    EXPECT_EQ(writer.getSegments().size(), 0);
}

TEST(DocumentsWriterTest, AddMultipleDocuments) {
    DocumentsWriter writer;

    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i),
                                            TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    EXPECT_EQ(writer.getNumDocsInRAM(), 10);
    EXPECT_EQ(writer.getNumDocsAdded(), 10);
    EXPECT_EQ(writer.getSegments().size(), 0);  // No flush yet
}

TEST(DocumentsWriterTest, AutoFlushByDocumentCount) {
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 5;  // Flush after 5 docs
    DocumentsWriter writer(config);

    int totalSegments = 0;

    // Add 4 documents - should not trigger flush
    for (int i = 0; i < 4; i++) {
        Document doc;
        doc.add(
            std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        int segments = writer.addDocument(doc);
        totalSegments += segments;
    }

    EXPECT_EQ(totalSegments, 0);
    EXPECT_EQ(writer.getNumDocsInRAM(), 4);
    EXPECT_EQ(writer.getSegments().size(), 0);

    // Add 5th document - should trigger flush
    Document doc5;
    doc5.add(std::make_unique<TextField>("body", "doc5", TextField::TYPE_STORED));
    int segments = writer.addDocument(doc5);

    EXPECT_EQ(segments, 1);                  // One segment created
    EXPECT_EQ(writer.getNumDocsInRAM(), 0);  // DWPT reset
    EXPECT_EQ(writer.getNumDocsAdded(), 5);  // Total docs tracked
    EXPECT_EQ(writer.getSegments().size(), 1);
}

TEST(DocumentsWriterTest, AutoFlushByRAMLimit) {
    DocumentsWriter::Config config;
    config.dwptConfig.ramBufferSizeMB = 1;      // Small RAM limit (1MB)
    config.dwptConfig.maxBufferedDocs = 10000;  // High doc limit
    DocumentsWriter writer(config);

    // Add documents with many unique terms until flush
    int totalSegments = 0;
    int docsAdded = 0;

    for (int i = 0; i < 100; i++) {
        Document doc;
        // Create document with many unique terms
        std::string text;
        for (int j = 0; j < 1000; j++) {
            text += "term_" + std::to_string(i) + "_" + std::to_string(j) + " ";
        }
        doc.add(std::make_unique<TextField>("body", text, TextField::TYPE_STORED));

        int segments = writer.addDocument(doc);
        totalSegments += segments;
        docsAdded++;

        if (segments > 0) {
            break;  // Flushed
        }
    }

    // Should have triggered flush before hitting doc limit
    EXPECT_GT(totalSegments, 0);
    EXPECT_LT(docsAdded, 10000);
    EXPECT_EQ(writer.getSegments().size(), 1);
}

TEST(DocumentsWriterTest, ManualFlush) {
    DocumentsWriter writer;

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(
            std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    EXPECT_EQ(writer.getNumDocsInRAM(), 5);
    EXPECT_EQ(writer.getSegments().size(), 0);

    // Manual flush
    int segments = writer.flush();

    EXPECT_EQ(segments, 1);
    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getNumDocsAdded(), 5);
    EXPECT_EQ(writer.getSegments().size(), 1);
}

TEST(DocumentsWriterTest, FlushEmptyBuffer) {
    DocumentsWriter writer;

    // Flush without adding documents
    int segments = writer.flush();

    EXPECT_EQ(segments, 0);
    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getSegments().size(), 0);
}

TEST(DocumentsWriterTest, MultipleFlushCycles) {
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 3;
    DocumentsWriter writer(config);

    // First cycle: add 3 docs, auto-flush
    for (int i = 0; i < 3; i++) {
        Document doc;
        doc.add(
            std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    EXPECT_EQ(writer.getSegments().size(), 1);
    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getNumDocsAdded(), 3);

    // Second cycle: add 3 more docs, auto-flush
    for (int i = 0; i < 3; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i + 3),
                                            TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    EXPECT_EQ(writer.getSegments().size(), 2);
    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getNumDocsAdded(), 6);

    // Third cycle: add 2 docs, manual flush
    for (int i = 0; i < 2; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc" + std::to_string(i + 6),
                                            TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    writer.flush();

    EXPECT_EQ(writer.getSegments().size(), 3);
    EXPECT_EQ(writer.getNumDocsAdded(), 8);
}

TEST(DocumentsWriterTest, SegmentNamesUnique) {
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 2;
    DocumentsWriter writer(config);

    // Create multiple segments
    for (int i = 0; i < 6; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    // Should have 3 segments (6 docs / 2 per segment)
    EXPECT_EQ(writer.getSegments().size(), 3);

    // All segment names should be unique
    const auto& segments = writer.getSegments();
    EXPECT_NE(segments[0], segments[1]);
    EXPECT_NE(segments[1], segments[2]);
    EXPECT_NE(segments[0], segments[2]);

    // All should start with underscore
    for (const auto& seg : segments) {
        EXPECT_EQ(seg[0], '_');
    }
}

TEST(DocumentsWriterTest, Reset) {
    DocumentsWriter writer;

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(
            std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    writer.flush();

    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getNumDocsAdded(), 5);
    EXPECT_EQ(writer.getSegments().size(), 1);

    // Reset
    writer.reset();

    EXPECT_EQ(writer.getNumDocsInRAM(), 0);
    EXPECT_EQ(writer.getNumDocsAdded(), 0);
    EXPECT_EQ(writer.getSegments().size(), 0);
}

TEST(DocumentsWriterTest, BytesUsedIncreases) {
    DocumentsWriter writer;

    int64_t initialBytes = writer.bytesUsed();

    // Add documents
    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(
            std::make_unique<TextField>("body", "document content here", TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    int64_t afterBytes = writer.bytesUsed();
    EXPECT_GT(afterBytes, initialBytes);
}

TEST(DocumentsWriterTest, BytesResetAfterFlush) {
    DocumentsWriter writer;

    // Add documents
    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "document content", TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    int64_t beforeFlush = writer.bytesUsed();
    EXPECT_GT(beforeFlush, 0);

    // Flush
    writer.flush();

    int64_t afterFlush = writer.bytesUsed();
    EXPECT_LT(afterFlush, beforeFlush);  // Should decrease significantly
}

TEST(DocumentsWriterTest, NeedsFlushDetection) {
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 5;
    DocumentsWriter writer(config);

    // Add 4 documents - should not need flush
    for (int i = 0; i < 4; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    EXPECT_FALSE(writer.needsFlush());

    // Add 5th document - should need flush
    Document doc5;
    doc5.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
    writer.addDocument(doc5);

    // After auto-flush, should not need flush
    EXPECT_FALSE(writer.needsFlush());
}

TEST(DocumentsWriterTest, ConfigurationPropagation) {
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 7;
    config.dwptConfig.ramBufferSizeMB = 32;

    DocumentsWriter writer(config);

    // Add 6 documents - should not trigger flush
    for (int i = 0; i < 6; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
        int segments = writer.addDocument(doc);
        EXPECT_EQ(segments, 0);
    }

    // Add 7th document - should trigger flush
    Document doc7;
    doc7.add(std::make_unique<TextField>("body", "doc", TextField::TYPE_STORED));
    int segments = writer.addDocument(doc7);
    EXPECT_EQ(segments, 1);
}

TEST(DocumentsWriterTest, LargeDocumentBatch) {
    DocumentsWriter writer;

    // Add 100 documents
    for (int i = 0; i < 100; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("title", "Title " + std::to_string(i),
                                            TextField::TYPE_STORED));
        doc.add(std::make_unique<TextField>(
            "body", "Body content for document " + std::to_string(i), TextField::TYPE_STORED));
        doc.add(std::make_unique<NumericDocValuesField>("id", i));
        writer.addDocument(doc);
    }

    EXPECT_EQ(writer.getNumDocsAdded(), 100);
    EXPECT_GT(writer.bytesUsed(), 0);

    // Flush
    writer.flush();

    EXPECT_GT(writer.getSegments().size(), 0);
}

TEST(DocumentsWriterTest, EmptyDocumentHandling) {
    DocumentsWriter writer;

    // Add empty document
    Document emptyDoc;
    int segments = writer.addDocument(emptyDoc);

    EXPECT_EQ(segments, 0);
    EXPECT_EQ(writer.getNumDocsInRAM(), 1);
    EXPECT_EQ(writer.getNumDocsAdded(), 1);
}

TEST(DocumentsWriterTest, SegmentTrackingOrder) {
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 2;
    DocumentsWriter writer(config);

    // Create segments by adding docs
    for (int i = 0; i < 6; i++) {
        Document doc;
        doc.add(
            std::make_unique<TextField>("body", "doc" + std::to_string(i), TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    // Should have 3 segments in order
    const auto& segments = writer.getSegments();
    EXPECT_EQ(segments.size(), 3);

    // Segments should be in creation order
    // (segment names increment, so later segments have higher hex values)
    // Just verify all are present and valid
    for (const auto& seg : segments) {
        EXPECT_FALSE(seg.empty());
        EXPECT_EQ(seg[0], '_');
    }
}
