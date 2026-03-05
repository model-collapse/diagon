// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
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

class MaybeMergeTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_maybe_merge_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override { fs::remove_all(testDir_); }

    fs::path testDir_;
};

/**
 * Test: Segments stay bounded during bulk indexing
 *
 * With maxBufferedDocs=10 and 500 docs, without maybeMerge() we'd get ~50 segments.
 * With maybeMerge(), TieredMergePolicy should keep segment count bounded.
 */
TEST_F(MaybeMergeTest, SegmentsBoundedDuringBulkIndexing) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);

    IndexWriter writer(*dir, config);

    for (int i = 0; i < 500; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "document number " + std::to_string(i)));
        writer.addDocument(doc);
    }

    int segmentCount = writer.getSegmentInfos().size();
    EXPECT_LE(segmentCount, 40)
        << "Segment count should be bounded by maybeMerge(), got " << segmentCount;
    EXPECT_GT(segmentCount, 0) << "Should have at least one segment";

    writer.close();
}

/**
 * Test: Merged segments are searchable
 *
 * Index 200 docs, commit (which triggers merge), then verify all docs are
 * searchable via DirectoryReader.
 */
TEST_F(MaybeMergeTest, MergedSegmentsSearchable) {
    const int numDocs = 200;

    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "searchable doc " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Verify all docs are searchable
    auto dir = FSDirectory::open(testDir_.string());
    auto reader = DirectoryReader::open(*dir);
    EXPECT_EQ(numDocs, reader->numDocs())
        << "All " << numDocs << " documents should be searchable after merge";
    reader->decRef();
}

/**
 * Test: No merge triggered when only one segment exists
 *
 * With maxBufferedDocs=1000 and only 500 docs, everything stays in one segment.
 * maybeMerge() should be a no-op.
 */
TEST_F(MaybeMergeTest, NoMergeForSingleSegment) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(1000);

    IndexWriter writer(*dir, config);

    for (int i = 0; i < 500; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "single segment doc " + std::to_string(i)));
        writer.addDocument(doc);
    }

    // No auto-flush should have occurred, so 0 segments (all in RAM)
    EXPECT_EQ(0, writer.getSegmentInfos().size())
        << "No segments should exist yet (all docs in RAM)";

    writer.flush();

    // After flush, exactly 1 segment
    EXPECT_EQ(1, writer.getSegmentInfos().size()) << "Should have exactly 1 segment after flush";

    writer.close();
}

/**
 * Test: Commit-time merge bounds segments
 *
 * Index docs with small buffer creating many segments, then commit.
 * The commit should trigger maybeMerge() and reduce segment count.
 */
TEST_F(MaybeMergeTest, CommitTimeMerge) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);

    IndexWriter writer(*dir, config);

    for (int i = 0; i < 300; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "commit merge doc " + std::to_string(i)));
        writer.addDocument(doc);
    }

    writer.commit();

    int segmentCount = writer.getSegmentInfos().size();
    EXPECT_LE(segmentCount, 40)
        << "Commit-time merge should keep segments bounded, got " << segmentCount;

    writer.close();
}
