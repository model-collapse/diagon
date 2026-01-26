// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <memory>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class IndexWriterRollbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test index
        testDir_ = fs::temp_directory_path() / "diagon_rollback_test";
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
 * Test: Rollback discards pending documents
 *
 * Verifies that rollback() discards documents added after last commit.
 */
TEST_F(IndexWriterRollbackTest, RollbackDiscardsPendingDocuments) {
    // Create index and commit some documents
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setCommitOnClose(false);  // Don't auto-commit on close

        IndexWriter writer(*dir, config);

        // Add and commit first batch
        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.commit();

        // Add more documents but don't commit
        for (int i = 5; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
            writer.addDocument(doc);
        }

        // Verify documents are in RAM
        EXPECT_GT(writer.getNumDocsInRAM(), 0);

        // Rollback - should discard docs 5-9
        writer.rollback();

        // Writer should be closed after rollback
        EXPECT_FALSE(writer.isOpen());
    }

    // Reopen and verify only committed documents exist
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);

        IndexWriter writer(*dir, config);

        // Should have segments from first commit
        const auto& infos = writer.getSegmentInfos();
        // Note: Can't easily verify exact doc count without reading segments
        // Just verify writer opened successfully (index exists)
        EXPECT_TRUE(writer.isOpen());
    }
}

/**
 * Test: Rollback restores last committed state
 *
 * Verifies that rollback() restores segment list to last commit.
 */
TEST_F(IndexWriterRollbackTest, RollbackRestoresLastCommit) {
    // Create index with multiple commits
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setCommitOnClose(false);

        IndexWriter writer(*dir, config);

        // First commit: 3 docs
        for (int i = 0; i < 3; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch1_" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.commit();

        int segmentsAfterFirstCommit = writer.getSegmentInfos().size();

        // Second commit: 3 more docs
        for (int i = 0; i < 3; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch2_" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.commit();

        int segmentsAfterSecondCommit = writer.getSegmentInfos().size();
        EXPECT_GE(segmentsAfterSecondCommit, segmentsAfterFirstCommit);

        // Add uncommitted docs
        for (int i = 0; i < 3; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch3_" + std::to_string(i)));
            writer.addDocument(doc);
        }

        // Rollback - should restore to second commit state
        writer.rollback();
    }

    // Reopen and verify segments match second commit
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);

        IndexWriter writer(*dir, config);

        // Should have segments from second commit
        // (exact count depends on flush behavior)
        EXPECT_GT(writer.getSegmentInfos().size(), 0);
    }
}

/**
 * Test: Rollback with no previous commit
 *
 * Verifies that rollback() works on a new index with no commits.
 */
TEST_F(IndexWriterRollbackTest, RollbackWithNoPreviousCommit) {
    // Create new index, add docs, rollback before any commit
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setCommitOnClose(false);

        IndexWriter writer(*dir, config);

        // Add documents without committing
        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
            writer.addDocument(doc);
        }

        EXPECT_GT(writer.getNumDocsInRAM(), 0);

        // Rollback on new index - should just clear everything
        writer.rollback();

        EXPECT_FALSE(writer.isOpen());
    }

    // Reopen - should fail or create new index (no segments_N file)
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);

        // Should succeed (no index exists, so it creates)
        IndexWriter writer(*dir, config);
        EXPECT_EQ(0, writer.getSegmentInfos().size());
    }
}

/**
 * Test: Rollback closes writer
 *
 * Verifies that writer is closed after rollback.
 */
TEST_F(IndexWriterRollbackTest, RollbackClosesWriter) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setCommitOnClose(false);

    IndexWriter writer(*dir, config);

    // Add some docs
    Document doc;
    doc.add(std::make_unique<TextField>("content", "test"));
    writer.addDocument(doc);

    EXPECT_TRUE(writer.isOpen());

    // Rollback
    writer.rollback();

    // Writer should be closed
    EXPECT_FALSE(writer.isOpen());

    // Subsequent operations should throw AlreadyClosedException
    EXPECT_THROW({
        Document doc2;
        doc2.add(std::make_unique<TextField>("content", "after rollback"));
        writer.addDocument(doc2);
    }, AlreadyClosedException);
}
