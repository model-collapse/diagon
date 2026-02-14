// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class ForceMergeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test index
        testDir_ = fs::temp_directory_path() / "diagon_merge_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override {
        // Cleanup test directory
        fs::remove_all(testDir_);
    }

    fs::path testDir_;
};

/**
 * Test: Force merge to 1 segment
 *
 * Verifies that forceMerge(1) merges all segments into one.
 */
TEST_F(ForceMergeTest, ForceMergeToOneSegment) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(5);  // Force multiple segments

    IndexWriter writer(*dir, config);

    // Add documents in batches to create multiple segments
    for (int batch = 0; batch < 3; batch++) {
        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch" + std::to_string(batch) +
                                                               "_doc" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.flush();  // Force segment creation
    }

    // Should have multiple segments
    int segmentsBeforeMerge = writer.getSegmentInfos().size();
    EXPECT_GT(segmentsBeforeMerge, 1) << "Should have created multiple segments";

    // Force merge to 1 segment
    writer.forceMerge(1);

    // Should have exactly 1 segment
    EXPECT_EQ(1, writer.getSegmentInfos().size()) << "Should have merged to 1 segment";

    writer.close();
}

/**
 * Test: Force merge to N segments
 *
 * Verifies that forceMerge(N) merges down to at most N segments.
 */
TEST_F(ForceMergeTest, ForceMergeToNSegments) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(5);

    IndexWriter writer(*dir, config);

    // Create 6 segments
    for (int batch = 0; batch < 6; batch++) {
        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch" + std::to_string(batch) +
                                                               "_doc" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.flush();
    }

    int segmentsBeforeMerge = writer.getSegmentInfos().size();
    EXPECT_GE(segmentsBeforeMerge, 6) << "Should have at least 6 segments";

    // Force merge to 3 segments
    writer.forceMerge(3);

    // Should have at most 3 segments
    int segmentsAfterMerge = writer.getSegmentInfos().size();
    EXPECT_LE(segmentsAfterMerge, 3) << "Should have merged to at most 3 segments";
    EXPECT_LT(segmentsAfterMerge, segmentsBeforeMerge) << "Should have reduced segment count";

    writer.close();
}

/**
 * Test: Force merge with pending documents
 *
 * Verifies that forceMerge() flushes pending docs before merging.
 */
TEST_F(ForceMergeTest, ForceMergeWithPendingDocs) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(100);  // High limit to keep docs in RAM

    IndexWriter writer(*dir, config);

    // Create committed segments
    for (int batch = 0; batch < 3; batch++) {
        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>(
                "content", "committed_batch" + std::to_string(batch) + "_doc" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.flush();
    }

    int segmentsBeforePending = writer.getSegmentInfos().size();
    (void)segmentsBeforePending;

    // Add pending docs in RAM
    for (int i = 0; i < 20; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "pending_doc" + std::to_string(i)));
        writer.addDocument(doc);
    }

    EXPECT_GT(writer.getNumDocsInRAM(), 0) << "Should have pending docs in RAM";

    // Force merge - should flush pending docs first
    writer.forceMerge(1);

    // Pending docs should be flushed
    EXPECT_EQ(0, writer.getNumDocsInRAM()) << "Pending docs should be flushed";

    // Should have 1 segment
    EXPECT_EQ(1, writer.getSegmentInfos().size());

    writer.close();
}

/**
 * Test: Force merge commits changes
 *
 * Verifies that forceMerge() commits the merged index.
 */
TEST_F(ForceMergeTest, ForceMergeCommitsChanges) {
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(5);
        config.setCommitOnClose(false);  // Don't auto-commit on close

        IndexWriter writer(*dir, config);

        // Create multiple segments
        for (int batch = 0; batch < 4; batch++) {
            for (int i = 0; i < 10; i++) {
                Document doc;
                doc.add(std::make_unique<TextField>("content", "batch" + std::to_string(batch) +
                                                                   "_doc" + std::to_string(i)));
                writer.addDocument(doc);
            }
            writer.flush();
        }

        // Force merge - should commit
        writer.forceMerge(1);

        writer.close();  // Close without commit
    }

    // Reopen - merged segment should be persisted
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);

        IndexWriter writer(*dir, config);

        // Should have 1 segment from merge
        EXPECT_EQ(1, writer.getSegmentInfos().size());
    }
}

/**
 * Test: Force merge with invalid parameter
 *
 * Verifies that forceMerge() throws on invalid maxNumSegments.
 */
TEST_F(ForceMergeTest, ForceMergeInvalidParameter) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    IndexWriter writer(*dir, config);

    // Add some docs
    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
        writer.addDocument(doc);
    }

    // Should throw on maxNumSegments < 1
    EXPECT_THROW(writer.forceMerge(0), std::invalid_argument);
    EXPECT_THROW(writer.forceMerge(-1), std::invalid_argument);

    // Valid call should succeed
    EXPECT_NO_THROW(writer.forceMerge(1));
}

/**
 * Test: Force merge with single segment
 *
 * Verifies that forceMerge() is no-op when already at target.
 */
TEST_F(ForceMergeTest, ForceMergeWithSingleSegment) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(100);  // Keep all docs in one segment

    IndexWriter writer(*dir, config);

    // Add docs
    for (int i = 0; i < 50; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
        writer.addDocument(doc);
    }
    writer.flush();

    // Should have 1 segment
    EXPECT_EQ(1, writer.getSegmentInfos().size());

    // Force merge to 1 - should be no-op
    writer.forceMerge(1);

    // Still 1 segment
    EXPECT_EQ(1, writer.getSegmentInfos().size());
}

/**
 * Test: Force merge with no segments
 *
 * Verifies that forceMerge() handles empty index.
 */
TEST_F(ForceMergeTest, ForceMergeWithNoSegments) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    IndexWriter writer(*dir, config);

    // No documents added
    EXPECT_EQ(0, writer.getSegmentInfos().size());

    // Force merge on empty index - should be no-op
    EXPECT_NO_THROW(writer.forceMerge(1));

    // Still no segments
    EXPECT_EQ(0, writer.getSegmentInfos().size());
}
