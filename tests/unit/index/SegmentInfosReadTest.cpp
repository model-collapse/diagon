// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentInfo.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <gtest/gtest.h>
#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

class SegmentInfosReadTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_segmentinfos_read_test";
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

    void writeTestIndex(int numDocs) {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);
        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc = createDocument("test " + std::to_string(i));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== Basic Read Tests ====================

TEST_F(SegmentInfosReadTest, ReadEmptyIndex) {
    // Empty directory should throw
    EXPECT_THROW(
        SegmentInfos::readLatestCommit(*dir),
        IOException
    );
}

TEST_F(SegmentInfosReadTest, ReadAfterWrite) {
    // Write index
    writeTestIndex(5);

    // Read back
    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);

    // Verify
    EXPECT_EQ(infos.getGeneration(), 0);  // First commit is generation 0
    EXPECT_GE(infos.size(), 1);  // At least one segment
    EXPECT_EQ(infos.totalMaxDoc(), 5);  // 5 documents total
}

TEST_F(SegmentInfosReadTest, ReadSpecificGeneration) {
    // Write index
    writeTestIndex(5);

    // Read specific generation
    std::string fileName = SegmentInfos::getSegmentsFileName(0);
    SegmentInfos infos = SegmentInfos::read(*dir, fileName);

    // Verify
    EXPECT_EQ(infos.getGeneration(), 0);
    EXPECT_GE(infos.size(), 1);
}

TEST_F(SegmentInfosReadTest, ReadNonExistentFile) {
    // Try to read non-existent file
    EXPECT_THROW(
        SegmentInfos::read(*dir, "segments_999"),
        std::exception
    );
}

// ==================== Segment Metadata Tests ====================

TEST_F(SegmentInfosReadTest, SegmentMetadataPreserved) {
    // Write index
    writeTestIndex(10);

    // Read back
    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);

    // Verify segment metadata
    for (int i = 0; i < infos.size(); i++) {
        auto seg = infos.info(i);

        // Check segment name format (_0, _1, etc.)
        EXPECT_EQ(seg->name()[0], '_');

        // Check maxDoc
        EXPECT_GT(seg->maxDoc(), 0);

        // Check codec name
        EXPECT_EQ(seg->codecName(), "Lucene104");

        // Check files list
        EXPECT_GT(seg->files().size(), 0);
        for (const auto& file : seg->files()) {
            EXPECT_TRUE(fs::exists(testDir_ / file));
        }

        // Check diagnostics
        EXPECT_EQ(seg->getDiagnostic("source"), "flush");

        // Check size (may be 0 if not set by writer)
        EXPECT_GE(seg->sizeInBytes(), 0);
    }
}

TEST_F(SegmentInfosReadTest, MultipleSegments) {
    // Write index with low flush limit to create multiple segments
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);  // Force multiple segments
        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc = createDocument("test " + std::to_string(i));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read back
    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);

    // Should have multiple segments
    EXPECT_GE(infos.size(), 2);

    // Total docs should be 10
    EXPECT_EQ(infos.totalMaxDoc(), 10);

    // Verify each segment
    int totalDocs = 0;
    for (int i = 0; i < infos.size(); i++) {
        auto seg = infos.info(i);
        totalDocs += seg->maxDoc();
        EXPECT_GT(seg->maxDoc(), 0);
    }
    EXPECT_EQ(totalDocs, 10);
}

// ==================== Multiple Generations Tests ====================

TEST_F(SegmentInfosReadTest, FindLatestGeneration) {
    // First commit
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);
        writer.addDocument(createDocument("first"));
        writer.commit();
        writer.close();
    }

    // Second commit
    {
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);
        IndexWriter writer(*dir, config);
        writer.addDocument(createDocument("second"));
        writer.commit();
        writer.close();
    }

    // Third commit
    {
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);
        IndexWriter writer(*dir, config);
        writer.addDocument(createDocument("third"));
        writer.commit();
        writer.close();
    }

    // Read latest should get generation 2 (third commit)
    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);
    EXPECT_EQ(infos.getGeneration(), 2);

    // Verify all segments files exist
    EXPECT_TRUE(fs::exists(testDir_ / "segments_0"));
    EXPECT_TRUE(fs::exists(testDir_ / "segments_1"));
    EXPECT_TRUE(fs::exists(testDir_ / "segments_2"));
}

TEST_F(SegmentInfosReadTest, ReadOlderGeneration) {
    // Write multiple commits
    for (int i = 0; i < 3; i++) {
        IndexWriterConfig config;
        if (i > 0) {
            config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);
        }
        IndexWriter writer(*dir, config);
        writer.addDocument(createDocument("commit " + std::to_string(i)));
        writer.commit();
        writer.close();
    }

    // Read generation 0
    SegmentInfos infos0 = SegmentInfos::read(*dir, "segments_0");
    EXPECT_EQ(infos0.getGeneration(), 0);

    // Read generation 1
    SegmentInfos infos1 = SegmentInfos::read(*dir, "segments_1");
    EXPECT_EQ(infos1.getGeneration(), 1);

    // Read generation 2 (latest)
    SegmentInfos infos2 = SegmentInfos::readLatestCommit(*dir);
    EXPECT_EQ(infos2.getGeneration(), 2);
}

// ==================== File Format Validation Tests ====================

TEST_F(SegmentInfosReadTest, InvalidMagicHeader) {
    // Create a file with invalid magic
    auto output = dir->createOutput("segments_bad", IOContext::DEFAULT);
    output->writeInt(0xDEADBEEF);  // Wrong magic
    output->writeInt(1);  // Version
    output->writeLong(0);  // Generation
    output->writeInt(0);  // No segments
    output->close();

    // Should throw on read
    EXPECT_THROW(
        SegmentInfos::read(*dir, "segments_bad"),
        IOException
    );
}

TEST_F(SegmentInfosReadTest, UnsupportedVersion) {
    // Create a file with unsupported version
    auto output = dir->createOutput("segments_bad_version", IOContext::DEFAULT);
    output->writeInt(0x3fd76c17);  // Correct magic
    output->writeInt(999);  // Unsupported version
    output->writeLong(0);  // Generation
    output->writeInt(0);  // No segments
    output->close();

    // Should throw on read
    EXPECT_THROW(
        SegmentInfos::read(*dir, "segments_bad_version"),
        IOException
    );
}

// ==================== Round-Trip Tests ====================

TEST_F(SegmentInfosReadTest, WriteReadRoundTrip) {
    // Write index with various documents
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(5);
        IndexWriter writer(*dir, config);

        for (int i = 0; i < 15; i++) {
            Document doc = createDocument("document number " + std::to_string(i));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read back
    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);

    // Verify all segments
    for (int i = 0; i < infos.size(); i++) {
        auto seg = infos.info(i);

        // All fields should be populated
        EXPECT_FALSE(seg->name().empty());
        EXPECT_GT(seg->maxDoc(), 0);
        EXPECT_FALSE(seg->codecName().empty());
        EXPECT_GT(seg->files().size(), 0);
        EXPECT_GE(seg->sizeInBytes(), 0);  // May be 0 if not set by writer

        // Files should exist on disk
        for (const auto& file : seg->files()) {
            EXPECT_TRUE(fs::exists(testDir_ / file))
                << "File " << file << " should exist";
        }
    }
}

TEST_F(SegmentInfosReadTest, LargeIndex) {
    // Write larger index
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);
        IndexWriter writer(*dir, config);

        for (int i = 0; i < 100; i++) {
            Document doc = createDocument("doc " + std::to_string(i));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read back
    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);

    // Verify
    EXPECT_EQ(infos.totalMaxDoc(), 100);
    EXPECT_GT(infos.size(), 0);

    // Verify segment names are unique
    std::set<std::string> names;
    for (int i = 0; i < infos.size(); i++) {
        auto seg = infos.info(i);
        EXPECT_TRUE(names.insert(seg->name()).second)
            << "Duplicate segment name: " << seg->name();
    }
}

// ==================== Edge Cases ====================

TEST_F(SegmentInfosReadTest, EmptyCommit) {
    // Commit with no documents
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);
        writer.commit();
        writer.close();
    }

    // Read back - should succeed even with no segments
    EXPECT_NO_THROW({
        SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);
        EXPECT_EQ(infos.size(), 0);
        EXPECT_EQ(infos.totalMaxDoc(), 0);
    });
}

TEST_F(SegmentInfosReadTest, SegmentWithNoDiagnostics) {
    // This is tested implicitly by all other tests
    // but we can verify explicitly
    writeTestIndex(5);

    SegmentInfos infos = SegmentInfos::readLatestCommit(*dir);
    for (int i = 0; i < infos.size(); i++) {
        auto seg = infos.info(i);
        // Should have at least the "source" diagnostic
        EXPECT_FALSE(seg->getDiagnostic("source").empty());
    }
}
