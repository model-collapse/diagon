// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include <unistd.h>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class MergeCommitPointTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_merge_commit_point_test_" + std::to_string(getpid()) + "_" +
                    std::to_string(counter++));
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override { fs::remove_all(testDir_); }

    fs::path testDir_;
};

/**
 * Test: commitMergeResults() persists merge results to disk
 *
 * Index enough documents to trigger background merges, then:
 *   1. commit() — flushes + writes segments_N + triggers merges
 *   2. waitForMerges() — waits for background merges to complete
 *   3. commitMergeResults() — writes updated segments_N
 *
 * Then read segments_N from disk and verify:
 *   - totalMaxDoc == numDocs (no duplication)
 *   - segment count is reduced (merges actually happened)
 */
TEST_F(MergeCommitPointTest, SourceSegmentsRemovedAfterMerge) {
    const int numDocs = 500;

    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(10);  // Force many small segments

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content",
                                                "merge commit point test doc " + std::to_string(i)));
            writer.addDocument(doc);
        }

        // Step 1: commit (flushes, writes segments_N, triggers background merges)
        writer.commit();

        // Step 2: wait for background merges to complete
        writer.waitForMerges();

        // Step 3: persist merge results to disk
        writer.commitMergeResults();

        writer.close();
    }

    // Verify: read segments_N from disk
    auto dir = FSDirectory::open(testDir_.string());
    auto committedInfos = SegmentInfos::readLatestCommit(*dir);

    // Calculate total maxDoc from on-disk segments_N
    int totalMaxDoc = 0;
    for (int i = 0; i < committedInfos.size(); i++) {
        totalMaxDoc += committedInfos.info(i)->maxDoc();
    }

    // CRITICAL: totalMaxDoc must equal numDocs — no duplication from stale source segments
    EXPECT_EQ(numDocs, totalMaxDoc)
        << "On-disk segments_N has totalMaxDoc=" << totalMaxDoc << " but only " << numDocs
        << " docs were indexed. Source segments were not removed after merge.";

    // Segments should have been merged (fewer than 50 segments for 500 docs @ 10 docs/flush)
    EXPECT_LT(committedInfos.size(), 50)
        << "Merge should have reduced segment count from ~50, got " << committedInfos.size();
}

/**
 * Test: Reader reopened after commitMergeResults sees correct doc count
 *
 * This simulates the CONJUGATE pattern: commit + waitForMerges + commitMergeResults + reopen reader
 */
TEST_F(MergeCommitPointTest, ReaderSeesCorrectDocCountAfterMergeCommit) {
    const int numDocs = 300;

    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "reader test doc " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.waitForMerges();
        writer.commitMergeResults();
        writer.close();
    }

    // Reopen from disk and verify — simulates node restart
    auto dir = FSDirectory::open(testDir_.string());
    auto reader = DirectoryReader::open(*dir);
    EXPECT_EQ(numDocs, reader->numDocs())
        << "Reader should see exactly " << numDocs << " docs, not duplicates from stale segments";
    reader->close();
}

/**
 * Test: commitMergeResults is safe to call when no merges happened
 *
 * If there were no background merges (e.g., single segment), commitMergeResults
 * should be a harmless no-op that just writes segments_N again.
 */
TEST_F(MergeCommitPointTest, CommitMergeResultsSafeWithNoMerges) {
    const int numDocs = 50;

    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(1000);  // Large buffer — no auto-flush, single segment

    IndexWriter writer(*dir, config);

    for (int i = 0; i < numDocs; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "no merge doc " + std::to_string(i)));
        writer.addDocument(doc);
    }

    writer.commit();
    writer.waitForMerges();

    // Should not throw even when no merges happened
    EXPECT_NO_THROW(writer.commitMergeResults());

    // Verify data integrity
    auto reader = DirectoryReader::open(*dir);
    EXPECT_EQ(numDocs, reader->numDocs());
    reader->close();

    writer.close();
}

/**
 * Test: triggerMerge() cascades and commitMergeResults persists cascade results
 *
 * Index lots of docs, commit, wait, then trigger additional merges and persist again.
 */
TEST_F(MergeCommitPointTest, TriggerMergeCascadeAndPersist) {
    const int numDocs = 500;

    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);

    IndexWriter writer(*dir, config);

    for (int i = 0; i < numDocs; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "cascade doc " + std::to_string(i)));
        writer.addDocument(doc);
    }

    // First round: commit + wait + persist
    writer.commit();
    writer.waitForMerges();
    writer.commitMergeResults();

    // Second round: trigger cascade merges + wait + persist
    writer.triggerMerge();
    writer.waitForMerges();
    writer.commitMergeResults();

    // Read from disk and verify
    auto committedInfos = SegmentInfos::readLatestCommit(*dir);
    int totalMaxDoc = 0;
    for (int i = 0; i < committedInfos.size(); i++) {
        totalMaxDoc += committedInfos.info(i)->maxDoc();
    }

    EXPECT_EQ(numDocs, totalMaxDoc)
        << "After cascade merge + commitMergeResults, totalMaxDoc should equal " << numDocs;

    writer.close();
}
