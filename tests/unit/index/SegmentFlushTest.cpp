// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DocumentsWriter.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class SegmentFlushTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test
        testDir_ = fs::temp_directory_path() / "diagon_flush_test";
        fs::create_directories(testDir_);
    }

    void TearDown() override {
        // Clean up test directory
        if (fs::exists(testDir_)) {
            fs::remove_all(testDir_);
        }
    }

    fs::path testDir_;
};

// ==================== DWPT Flush Tests ====================

TEST_F(SegmentFlushTest, FlushWithDirectory) {
    // Open directory
    auto dir = FSDirectory::open(testDir_.string());

    // Create DWPT with directory
    DocumentsWriterPerThread::Config config;
    config.maxBufferedDocs = 10;  // Low limit for quick flush
    DocumentsWriterPerThread dwpt(config, dir.get());

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>(
            "body", "term1 term2 term3 unique_term_" + std::to_string(i), TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    // Flush
    auto segmentInfo = dwpt.flush();

    // Verify segment info
    ASSERT_NE(segmentInfo, nullptr);
    EXPECT_FALSE(segmentInfo->name().empty());
    EXPECT_EQ(segmentInfo->maxDoc(), 5);
    EXPECT_EQ(segmentInfo->codecName(), "Lucene104");

    // Verify files were created
    EXPECT_GT(segmentInfo->files().size(), 0);

    // Check that postings-related file exists (.doc for Lucene104, .post for Simple)
    bool hasPostingsFile = false;
    for (const auto& file : segmentInfo->files()) {
        if (file.find(".doc") != std::string::npos || file.find(".post") != std::string::npos) {
            hasPostingsFile = true;

            // Verify file exists on disk
            fs::path filePath = testDir_ / file;
            EXPECT_TRUE(fs::exists(filePath));
            EXPECT_GT(fs::file_size(filePath), 0);
        }
    }
    EXPECT_TRUE(hasPostingsFile);

    // Verify diagnostics
    EXPECT_EQ(segmentInfo->getDiagnostic("source"), "flush");

    dir->close();
}

TEST_F(SegmentFlushTest, FlushWithoutDirectory) {
    // Create DWPT without directory
    DocumentsWriterPerThread dwpt;

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }

    // Flush (should still work, just no files written)
    auto segmentInfo = dwpt.flush();

    // Verify segment info exists but has no files
    ASSERT_NE(segmentInfo, nullptr);
    EXPECT_FALSE(segmentInfo->name().empty());
    EXPECT_EQ(segmentInfo->maxDoc(), 5);
    EXPECT_EQ(segmentInfo->files().size(), 0);  // No files written
}

TEST_F(SegmentFlushTest, MultipleFlushesToDisk) {
    // Open directory
    auto dir = FSDirectory::open(testDir_.string());

    // Create DWPT with directory
    DocumentsWriterPerThread dwpt(DocumentsWriterPerThread::Config{}, dir.get());

    // First flush
    for (int i = 0; i < 3; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "first_" + std::to_string(i),
                                            TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }
    auto segment1 = dwpt.flush();
    ASSERT_NE(segment1, nullptr);

    // Second flush
    for (int i = 0; i < 3; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "second_" + std::to_string(i),
                                            TextField::TYPE_STORED));
        dwpt.addDocument(doc);
    }
    auto segment2 = dwpt.flush();
    ASSERT_NE(segment2, nullptr);

    // Verify different segments
    EXPECT_NE(segment1->name(), segment2->name());

    // Verify both have files
    EXPECT_GT(segment1->files().size(), 0);
    EXPECT_GT(segment2->files().size(), 0);

    // Verify all files exist
    for (const auto& file : segment1->files()) {
        EXPECT_TRUE(fs::exists(testDir_ / file));
    }
    for (const auto& file : segment2->files()) {
        EXPECT_TRUE(fs::exists(testDir_ / file));
    }

    dir->close();
}

// ==================== DocumentsWriter Flush Tests ====================

TEST_F(SegmentFlushTest, DocumentsWriterFlush) {
    // Open directory
    auto dir = FSDirectory::open(testDir_.string());

    // Create DocumentsWriter with directory
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 10;
    DocumentsWriter writer(config, dir.get());

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "test_" + std::to_string(i),
                                            TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    // Manual flush
    int segmentsCreated = writer.flush();
    EXPECT_EQ(segmentsCreated, 1);

    // Verify segment was tracked
    EXPECT_EQ(writer.getSegments().size(), 1);
    EXPECT_EQ(writer.getSegmentInfos().size(), 1);

    // Verify segment info
    auto segmentInfo = writer.getSegmentInfos()[0];
    ASSERT_NE(segmentInfo, nullptr);
    EXPECT_EQ(segmentInfo->maxDoc(), 5);
    EXPECT_GT(segmentInfo->files().size(), 0);

    // Verify files exist
    for (const auto& file : segmentInfo->files()) {
        EXPECT_TRUE(fs::exists(testDir_ / file));
    }

    dir->close();
}

TEST_F(SegmentFlushTest, DocumentsWriterAutoFlush) {
    // Open directory
    auto dir = FSDirectory::open(testDir_.string());

    // Create DocumentsWriter with low doc limit
    DocumentsWriter::Config config;
    config.dwptConfig.maxBufferedDocs = 5;  // Low limit for auto-flush
    DocumentsWriter writer(config, dir.get());

    // Add documents - should trigger auto-flush
    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", "test_" + std::to_string(i),
                                            TextField::TYPE_STORED));
        int segmentsCreated = writer.addDocument(doc);

        if (i == 4) {
            // Should trigger flush after 5th doc
            EXPECT_EQ(segmentsCreated, 1);
        }
    }

    // Should have at least one segment from auto-flush
    EXPECT_GE(writer.getSegmentInfos().size(), 1);

    // Verify all segment files exist
    for (const auto& segmentInfo : writer.getSegmentInfos()) {
        for (const auto& file : segmentInfo->files()) {
            EXPECT_TRUE(fs::exists(testDir_ / file));
        }
    }

    dir->close();
}

TEST_F(SegmentFlushTest, LargeDocumentFlush) {
    // Open directory
    auto dir = FSDirectory::open(testDir_.string());

    // Create DWPT with directory
    DocumentsWriterPerThread dwpt(DocumentsWriterPerThread::Config{}, dir.get());

    // Create large document with many unique terms
    Document doc;
    std::string content;
    for (int i = 0; i < 1000; i++) {
        content += "term_" + std::to_string(i) + " ";
    }
    doc.add(std::make_unique<TextField>("body", content, TextField::TYPE_STORED));
    dwpt.addDocument(doc);

    // Flush
    auto segmentInfo = dwpt.flush();

    // Verify
    ASSERT_NE(segmentInfo, nullptr);
    EXPECT_EQ(segmentInfo->maxDoc(), 1);
    EXPECT_GT(segmentInfo->files().size(), 0);

    // Verify files exist and postings file is reasonable size (has 1000 terms)
    bool foundLargeFile = false;
    for (const auto& file : segmentInfo->files()) {
        fs::path filePath = testDir_ / file;
        EXPECT_TRUE(fs::exists(filePath));
        auto fsize = fs::file_size(filePath);
        // The .doc file (postings) should be large; metadata files (.tmd, .tip) are small
        if (fsize > 100) {
            foundLargeFile = true;
        }
    }
    EXPECT_TRUE(foundLargeFile) << "At least one file should contain significant data";

    dir->close();
}

// ==================== Error Handling Tests ====================

TEST_F(SegmentFlushTest, FlushEmptyWithDirectory) {
    // Open directory
    auto dir = FSDirectory::open(testDir_.string());

    // Create DWPT with directory
    DocumentsWriterPerThread dwpt(DocumentsWriterPerThread::Config{}, dir.get());

    // Flush without adding documents
    auto segmentInfo = dwpt.flush();

    // Should return nullptr (nothing to flush)
    EXPECT_EQ(segmentInfo, nullptr);

    // No files should be created
    EXPECT_TRUE(fs::is_empty(testDir_));

    dir->close();
}
