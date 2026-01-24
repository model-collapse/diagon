// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

class IndexWriterIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_writer_integration_test";
        fs::create_directories(testDir_);
        dir = FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        if (dir) {
            dir->close();
        }
        if (fs::exists(testDir_)) {
            fs::remove_all(testDir_);
        }
    }

    Document createDocument(const std::string& content) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", content, TextField::TYPE_STORED));
        return doc;
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== End-to-End Tests ====================

TEST_F(IndexWriterIntegrationTest, AddDocumentsAndCommit) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(10);  // Low limit for testing

    IndexWriter writer(*dir, config);

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc = createDocument("test content " + std::to_string(i));
        writer.addDocument(doc);
    }

    // Check buffered docs
    EXPECT_EQ(writer.getNumDocsAdded(), 5);

    // Commit
    writer.commit();

    // Verify segments_N file was created
    auto files = dir->listAll();
    bool foundSegmentsFile = false;
    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            foundSegmentsFile = true;
            break;
        }
    }
    EXPECT_TRUE(foundSegmentsFile);

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, AutoFlushCreatesSegments) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(5);  // Low limit to trigger auto-flush

    IndexWriter writer(*dir, config);

    // Add documents - should trigger auto-flush after 5
    for (int i = 0; i < 10; i++) {
        Document doc = createDocument("test_" + std::to_string(i));
        writer.addDocument(doc);
    }

    // Should have created segments via auto-flush
    const auto& segmentInfos = writer.getSegmentInfos();
    EXPECT_GE(segmentInfos.size(), 1);  // At least one segment

    // Verify segment files exist
    for (int i = 0; i < segmentInfos.size(); i++) {
        auto segmentInfo = segmentInfos.info(i);
        for (const auto& file : segmentInfo->files()) {
            EXPECT_TRUE(fs::exists(testDir_ / file));
        }
    }

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, ManualFlushWithoutCommit) {
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc = createDocument("content_" + std::to_string(i));
        writer.addDocument(doc);
    }

    // Manual flush (does not write segments_N)
    writer.flush();

    // Check that segment files were created
    const auto& segmentInfos = writer.getSegmentInfos();
    EXPECT_GE(segmentInfos.size(), 1);

    // Verify .post files exist
    for (int i = 0; i < segmentInfos.size(); i++) {
        auto segmentInfo = segmentInfos.info(i);
        for (const auto& file : segmentInfo->files()) {
            EXPECT_TRUE(fs::exists(testDir_ / file));
        }
    }

    // But segments_N file should NOT exist yet
    auto files = dir->listAll();
    bool foundSegmentsFile = false;
    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            foundSegmentsFile = true;
        }
    }
    EXPECT_FALSE(foundSegmentsFile);

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, MultipleCommitsIncrementGeneration) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(5);

    IndexWriter writer(*dir, config);

    // First commit
    for (int i = 0; i < 3; i++) {
        Document doc = createDocument("first_" + std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    int64_t gen1 = writer.getSegmentInfos().getGeneration();

    // Second commit
    for (int i = 0; i < 3; i++) {
        Document doc = createDocument("second_" + std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    int64_t gen2 = writer.getSegmentInfos().getGeneration();

    // Generation should increase
    EXPECT_GT(gen2, gen1);

    // Verify multiple segments_N files
    auto files = dir->listAll();
    int segmentsFileCount = 0;
    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            segmentsFileCount++;
        }
    }
    EXPECT_GE(segmentsFileCount, 2);

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, CommitWithMultipleSegments) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(5);  // Low limit to create multiple segments

    IndexWriter writer(*dir, config);

    // Add enough documents to create multiple segments
    for (int i = 0; i < 15; i++) {
        Document doc = createDocument("test_" + std::to_string(i));
        writer.addDocument(doc);
    }

    // Commit
    writer.commit();

    // Should have multiple segments
    const auto& segmentInfos = writer.getSegmentInfos();
    EXPECT_GE(segmentInfos.size(), 2);

    // Calculate total docs
    int totalDocs = segmentInfos.totalMaxDoc();
    EXPECT_EQ(totalDocs, 15);

    // Verify all segment files exist
    for (int i = 0; i < segmentInfos.size(); i++) {
        auto segmentInfo = segmentInfos.info(i);
        EXPECT_GT(segmentInfo->maxDoc(), 0);
        for (const auto& file : segmentInfo->files()) {
            EXPECT_TRUE(fs::exists(testDir_ / file));
        }
    }

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, CommitOnClose) {
    IndexWriterConfig config;
    config.setCommitOnClose(true);
    config.setMaxBufferedDocs(10);

    {
        IndexWriter writer(*dir, config);

        // Add documents
        for (int i = 0; i < 5; i++) {
            Document doc = createDocument("test_" + std::to_string(i));
            writer.addDocument(doc);
        }

        // Close without explicit commit (destructor should commit)
    }

    // Verify segments_N file was created
    auto files = dir->listAll();
    bool foundSegmentsFile = false;
    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            foundSegmentsFile = true;
            break;
        }
    }
    EXPECT_TRUE(foundSegmentsFile);
}

TEST_F(IndexWriterIntegrationTest, LargeDocumentBatch) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(100);

    IndexWriter writer(*dir, config);

    // Add many documents
    for (int i = 0; i < 500; i++) {
        Document doc = createDocument("document_" + std::to_string(i));
        writer.addDocument(doc);
    }

    // Commit
    writer.commit();

    // Verify
    EXPECT_EQ(writer.getNumDocsAdded(), 500);
    const auto& segmentInfos = writer.getSegmentInfos();
    EXPECT_GE(segmentInfos.size(), 1);
    EXPECT_EQ(segmentInfos.totalMaxDoc(), 500);

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, SegmentInfoMetadata) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(10);

    IndexWriter writer(*dir, config);

    // Add documents to create a segment
    for (int i = 0; i < 10; i++) {
        Document doc = createDocument("test_" + std::to_string(i));
        writer.addDocument(doc);
    }

    writer.flush();

    // Check segment metadata
    const auto& segmentInfos = writer.getSegmentInfos();
    ASSERT_GE(segmentInfos.size(), 1);

    auto segmentInfo = segmentInfos.info(0);

    // Verify segment name format
    EXPECT_EQ(segmentInfo->name()[0], '_');

    // Verify maxDoc
    EXPECT_EQ(segmentInfo->maxDoc(), 10);

    // Verify codec name
    EXPECT_EQ(segmentInfo->codecName(), "Lucene104");

    // Verify files list
    EXPECT_GT(segmentInfo->files().size(), 0);

    // Verify diagnostics
    EXPECT_EQ(segmentInfo->getDiagnostic("source"), "flush");

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, CreateModeOverwritesExisting) {
    // First writer creates index
    {
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        IndexWriter writer(*dir, config);

        Document doc = createDocument("first");
        writer.addDocument(doc);
        writer.commit();
        writer.close();
    }

    // Second writer with CREATE mode should overwrite
    {
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        IndexWriter writer(*dir, config);

        Document doc = createDocument("second");
        writer.addDocument(doc);
        writer.commit();

        // Should have only new data
        EXPECT_EQ(writer.getNumDocsAdded(), 1);

        writer.close();
    }
}

TEST_F(IndexWriterIntegrationTest, CreateOrAppendMode) {
    // First writer creates index
    {
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);
        IndexWriter writer(*dir, config);

        Document doc = createDocument("first");
        writer.addDocument(doc);
        writer.commit();
        writer.close();
    }

    // Second writer appends
    {
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);
        IndexWriter writer(*dir, config);

        Document doc = createDocument("second");
        writer.addDocument(doc);
        writer.commit();
        writer.close();
    }

    // Verify files exist
    EXPECT_GT(dir->listAll().size(), 0);
}

// ==================== Statistics Tests ====================

TEST_F(IndexWriterIntegrationTest, GetNumDocsInRAM) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(100);  // High limit to prevent auto-flush

    IndexWriter writer(*dir, config);

    EXPECT_EQ(writer.getNumDocsInRAM(), 0);

    for (int i = 0; i < 5; i++) {
        Document doc = createDocument("test_" + std::to_string(i));
        writer.addDocument(doc);
    }

    EXPECT_EQ(writer.getNumDocsInRAM(), 5);

    writer.flush();

    EXPECT_EQ(writer.getNumDocsInRAM(), 0);

    writer.close();
}

TEST_F(IndexWriterIntegrationTest, GetNumDocsAdded) {
    IndexWriterConfig config;
    config.setMaxBufferedDocs(5);

    IndexWriter writer(*dir, config);

    EXPECT_EQ(writer.getNumDocsAdded(), 0);

    for (int i = 0; i < 12; i++) {
        Document doc = createDocument("test_" + std::to_string(i));
        writer.addDocument(doc);
    }

    // Should track total docs added, even across flushes
    EXPECT_EQ(writer.getNumDocsAdded(), 12);

    writer.close();
}
