// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class ConcurrentMergeTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_concurrent_merge_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override { fs::remove_all(testDir_); }

    fs::path testDir_;
};

/**
 * Test: Background merge does not block addDocument
 *
 * Index 1000 docs with small buffer, verify P99 per-doc latency stays low.
 * With synchronous merging, some addDocument calls would take >1ms (merge I/O).
 * With background merging, all addDocument calls should be fast.
 */
TEST_F(ConcurrentMergeTest, BackgroundMergeDoesNotBlockIndexing) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);

    IndexWriter writer(*dir, config);

    std::vector<double> latenciesUs;
    latenciesUs.reserve(1000);

    for (int i = 0; i < 1000; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content",
                                            "background merge test doc " + std::to_string(i)));

        auto start = std::chrono::high_resolution_clock::now();
        writer.addDocument(doc);
        auto end = std::chrono::high_resolution_clock::now();

        double us = std::chrono::duration<double, std::micro>(end - start).count();
        latenciesUs.push_back(us);
    }

    // Sort latencies for percentile calculation
    std::sort(latenciesUs.begin(), latenciesUs.end());
    double p50 = latenciesUs[latenciesUs.size() / 2];
    double p99 = latenciesUs[static_cast<size_t>(latenciesUs.size() * 0.99)];

    // P99 should not be excessively high — merges should not block the indexing thread.
    // With sync merging, P99 could be >10ms due to merge I/O.
    // With background merging, P99 should be bounded by flush cost only.
    // We use a generous 5ms threshold to avoid flakiness.
    EXPECT_LT(p99, 5000.0) << "P99 addDocument latency " << p99 << " µs is too high — "
                           << "background merge may be blocking. P50=" << p50 << " µs";

    writer.close();
}

/**
 * Test: Merged files are cleaned up
 *
 * Index enough docs to trigger merges, commit, then verify no stale files remain.
 * Every segment in segmentInfos should have its files on disk, and no orphaned
 * _merged_N files should exist beyond what's referenced.
 */
TEST_F(ConcurrentMergeTest, MergedFilesCleanedUp) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);

    IndexWriter writer(*dir, config);

    for (int i = 0; i < 300; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "cleanup test doc " + std::to_string(i)));
        writer.addDocument(doc);
    }

    writer.commit();

    // With deferred deletion, background merges that complete after commit()
    // leave orphaned files until the next commit. Wait + re-commit to flush them.
    writer.waitForMerges();
    writer.commit();

    // Collect all files referenced by current segments
    std::set<std::string> referencedFiles;
    const auto& segInfos = writer.getSegmentInfos();
    for (int i = 0; i < segInfos.size(); i++) {
        for (const auto& f : segInfos.info(i)->files()) {
            referencedFiles.insert(f);
        }
    }

    // List actual directory files
    auto allFiles = dir->listAll();
    for (const auto& file : allFiles) {
        // Skip segments_N and write.lock
        if (file.find("segments_") == 0 || file == "write.lock") {
            continue;
        }
        // Every non-metadata file should be referenced by a segment
        EXPECT_TRUE(referencedFiles.count(file) > 0) << "Orphaned file found: " << file;
    }

    writer.close();
}

/**
 * Test: waitForMerges completes before commit writes segments_N
 *
 * Index docs triggering merges, commit. Reopen and verify all segments are
 * accessible — if a merge were still in-flight during commit, the segments_N
 * file would reference segments that don't exist yet.
 */
TEST_F(ConcurrentMergeTest, WaitForMergesBeforeCommit) {
    const int numDocs = 500;

    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "commit test doc " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Reopen and verify all docs are accessible
    auto dir = FSDirectory::open(testDir_.string());
    auto reader = DirectoryReader::open(*dir);
    EXPECT_EQ(numDocs, reader->numDocs())
        << "All documents should be readable after commit with background merges";
    reader->close();
}

/**
 * Test: Concurrent indexing from multiple threads
 *
 * Multiple threads call addDocument concurrently. Verify no crashes and all
 * documents are present after commit.
 */
TEST_F(ConcurrentMergeTest, ConcurrentIndexingAndMerging) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(20);

    auto writer = std::make_shared<IndexWriter>(*dir, config);

    const int numThreads = 4;
    const int docsPerThread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&writer, t]() {
            for (int i = 0; i < docsPerThread; i++) {
                Document doc;
                doc.add(std::make_unique<TextField>("content", "thread" + std::to_string(t) +
                                                                   "_doc" + std::to_string(i)));
                writer->addDocument(doc);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    writer->commit();

    // Verify all docs
    auto reader = DirectoryReader::open(*dir);
    EXPECT_EQ(numThreads * docsPerThread, reader->numDocs())
        << "All documents from all threads should be present";
    reader->close();

    writer->close();
}

/**
 * Test: Shutdown waits for pending merges
 *
 * Index docs to trigger merges, then immediately close the writer.
 * The close should block until all pending merges complete — no orphaned
 * threads, no data loss.
 */
TEST_F(ConcurrentMergeTest, ShutdownWaitsForPendingMerge) {
    {
        auto dir = FSDirectory::open(testDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 500; i++) {
            Document doc;
            doc.add(
                std::make_unique<TextField>("content", "shutdown test doc " + std::to_string(i)));
            writer.addDocument(doc);
        }

        // Commit + close — close should wait for pending merges, not crash
        writer.commit();
        writer.close();
    }

    // If we get here without hanging or crashing, the test passes.
    // Also verify the index is openable (not corrupted by incomplete merges).
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);
    EXPECT_NO_THROW({
        IndexWriter writer(*dir, config);
        writer.close();
    });
}

/**
 * Test: All docs searchable after background merge
 *
 * Index N docs (triggering background merges), commit, verify DirectoryReader
 * sees all N docs. This ensures no data loss during background merge operations.
 */
TEST_F(ConcurrentMergeTest, AllDocsSearchableAfterBackgroundMerge) {
    const int numDocs = 1000;

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

    // Verify all docs searchable
    auto dir = FSDirectory::open(testDir_.string());
    auto reader = DirectoryReader::open(*dir);
    EXPECT_EQ(numDocs, reader->numDocs())
        << "All " << numDocs << " documents should be searchable after background merges + commit";
    reader->close();
}
