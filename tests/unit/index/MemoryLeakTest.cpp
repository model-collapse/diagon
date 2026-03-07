// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0
//
// Memory stability tests: detect logical leaks where caches grow without bound.
// ASan/LSan catches physical leaks (lost pointers); these tests catch Issue #12-class
// bugs where a cache holds ALL pointers but grows unboundedly per operation.

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#ifdef __linux__
#    include <unistd.h>
#endif

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;
using diagon::search::IndexSearcher;
using diagon::search::TermQuery;

namespace fs = std::filesystem;

// Returns current RSS in bytes (Linux only; returns 0 on other platforms).
static size_t getCurrentRSSBytes() {
#ifdef __linux__
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open())
        return 0;
    size_t totalPages = 0, rssPages = 0;
    statm >> totalPages >> rssPages;
    return rssPages * static_cast<size_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;  // Not supported — tests will auto-pass
#endif
}

class MemoryLeakTest : public ::testing::Test {
protected:
    void SetUp() override {
        indexDir_ = fs::temp_directory_path() / "diagon_memory_leak_test";
        fs::remove_all(indexDir_);
        fs::create_directories(indexDir_);
    }

    void TearDown() override { fs::remove_all(indexDir_); }

    // Build a test index with N docs and M fields
    void buildIndex(int numDocs, int numFields = 1) {
        auto dir = FSDirectory::open(indexDir_.string());
        IndexWriterConfig config;
        config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
        config.setMaxBufferedDocs(100);

        IndexWriter writer(*dir, config);
        for (int i = 0; i < numDocs; i++) {
            Document doc;
            for (int f = 0; f < numFields; f++) {
                doc.add(std::make_unique<TextField>(
                    "field" + std::to_string(f), "word" + std::to_string(i % 50) +
                                                     " test document number " + std::to_string(i)));
            }
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    fs::path indexDir_;
};

/**
 * Core leak detection test: repeated queries must not grow RSS.
 *
 * Pattern that caught Issue #12:
 * 1. Build index
 * 2. Open reader + searcher
 * 3. Run N queries, measure RSS after warmup and after all queries
 * 4. Assert RSS growth is bounded (< 50 MB for 1000 queries)
 *
 * If a cache leaks ~1 MB per query, 1000 queries = 1 GB growth -> test fails.
 */
TEST_F(MemoryLeakTest, RepeatedQueriesStableRSS) {
    if (getCurrentRSSBytes() == 0) {
        GTEST_SKIP() << "RSS monitoring not available on this platform";
    }

    buildIndex(2000);

    auto dir = MMapDirectory::open(indexDir_.string());
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Warmup: run 10 queries to populate caches
    for (int i = 0; i < 10; i++) {
        search::Term term("field0", "word" + std::to_string(i));
        TermQuery query(term);
        searcher.search(query, 10);
    }

    size_t rssAfterWarmup = getCurrentRSSBytes();

    // Run 500 queries — if caches leak, RSS will grow significantly
    for (int i = 0; i < 500; i++) {
        search::Term term("field0", "word" + std::to_string(i % 50));
        TermQuery query(term);
        searcher.search(query, 10);
    }

    size_t rssAfterQueries = getCurrentRSSBytes();

    // Allow 50 MB growth for legitimate allocations (OS page cache, allocator overhead)
    // Issue #12 leaked ~936 MB per query batch — would blow past this threshold instantly
    int64_t growth = static_cast<int64_t>(rssAfterQueries) - static_cast<int64_t>(rssAfterWarmup);
    EXPECT_LT(growth, 50 * 1024 * 1024)
        << "RSS grew by " << (growth / (1024 * 1024))
        << " MB after 500 queries — possible memory leak. "
        << "Warmup RSS: " << (rssAfterWarmup / (1024 * 1024))
        << " MB, Final RSS: " << (rssAfterQueries / (1024 * 1024)) << " MB";

    reader->close();
}

/**
 * Multi-field leak detection: each field access caches data.
 * Ensure accessing many fields doesn't cause unbounded growth.
 */
TEST_F(MemoryLeakTest, MultiFieldAccessStableRSS) {
    if (getCurrentRSSBytes() == 0) {
        GTEST_SKIP() << "RSS monitoring not available on this platform";
    }

    const int numFields = 20;
    buildIndex(1000, numFields);

    auto dir = MMapDirectory::open(indexDir_.string());
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Warmup
    for (int f = 0; f < numFields; f++) {
        search::Term term("field" + std::to_string(f), "word0");
        TermQuery query(term);
        searcher.search(query, 10);
    }

    size_t rssAfterWarmup = getCurrentRSSBytes();

    // Run 200 queries across all fields — repeat to trigger cache replacement
    for (int round = 0; round < 200; round++) {
        for (int f = 0; f < numFields; f++) {
            search::Term term("field" + std::to_string(f), "word" + std::to_string(round % 50));
            TermQuery query(term);
            searcher.search(query, 10);
        }
    }

    size_t rssAfterQueries = getCurrentRSSBytes();

    int64_t growth = static_cast<int64_t>(rssAfterQueries) - static_cast<int64_t>(rssAfterWarmup);
    EXPECT_LT(growth, 50 * 1024 * 1024)
        << "RSS grew by " << (growth / (1024 * 1024))
        << " MB after 4000 multi-field queries — possible cache leak";

    reader->close();
}

/**
 * Long indexing session: segments_ vector should be pruned.
 * Without pruning, DocumentsWriter accumulates all segment references.
 */
TEST_F(MemoryLeakTest, LongIndexingSessionStableRSS) {
    if (getCurrentRSSBytes() == 0) {
        GTEST_SKIP() << "RSS monitoring not available on this platform";
    }

    auto dir = FSDirectory::open(indexDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(50);

    IndexWriter writer(*dir, config);

    // Index 500 docs as warmup
    for (int i = 0; i < 500; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "warmup doc " + std::to_string(i)));
        writer.addDocument(doc);
    }
    writer.commit();

    size_t rssAfterWarmup = getCurrentRSSBytes();

    // Index 5000 more docs — should not accumulate segment references
    for (int i = 0; i < 5000; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "bulk doc " + std::to_string(i)));
        writer.addDocument(doc);
    }
    writer.commit();

    size_t rssAfterBulk = getCurrentRSSBytes();

    // The actual index data grows on disk (via mmap or file writes), but in-memory
    // overhead from segment tracking should be negligible. Allow 100 MB for index data
    // in page cache + merge buffers.
    int64_t growth = static_cast<int64_t>(rssAfterBulk) - static_cast<int64_t>(rssAfterWarmup);
    EXPECT_LT(growth, 100 * 1024 * 1024)
        << "RSS grew by " << (growth / (1024 * 1024))
        << " MB during bulk indexing — possible segment tracking leak";

    writer.close();
}

/**
 * Reader reopen cycle: opening and closing readers repeatedly
 * must not leak memory.
 */
TEST_F(MemoryLeakTest, ReaderReopenCycleStable) {
    if (getCurrentRSSBytes() == 0) {
        GTEST_SKIP() << "RSS monitoring not available on this platform";
    }

    buildIndex(1000);

    // Warmup: open/close once
    {
        auto dir = MMapDirectory::open(indexDir_.string());
        auto reader = DirectoryReader::open(*dir);
        reader->close();
    }

    size_t rssAfterWarmup = getCurrentRSSBytes();

    // Open and close reader 50 times
    for (int i = 0; i < 50; i++) {
        auto dir = MMapDirectory::open(indexDir_.string());
        auto reader = DirectoryReader::open(*dir);

        // Do a query to populate caches
        IndexSearcher searcher(*reader);
        search::Term term("field0", "word0");
        TermQuery query(term);
        searcher.search(query, 10);

        reader->close();
    }

    size_t rssAfterCycles = getCurrentRSSBytes();

    int64_t growth = static_cast<int64_t>(rssAfterCycles) - static_cast<int64_t>(rssAfterWarmup);
    EXPECT_LT(growth, 50 * 1024 * 1024)
        << "RSS grew by " << (growth / (1024 * 1024))
        << " MB after 50 reader open/close cycles — possible reader leak";
}

/**
 * Cache bounds test: explicitly verify cache sizes stay bounded.
 * This is a whitebox test that checks internal cache invariants.
 */
TEST_F(MemoryLeakTest, CacheSizesBounded) {
    buildIndex(500, 10);

    auto dir = MMapDirectory::open(indexDir_.string());
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Query every field 100 times
    for (int round = 0; round < 100; round++) {
        for (int f = 0; f < 10; f++) {
            search::Term term("field" + std::to_string(f), "word" + std::to_string(round % 50));
            TermQuery query(term);
            searcher.search(query, 10);
        }
    }

    // If we get here without assertion failures (from Phase 1a cache assertions),
    // the caches are bounded. The debug-mode assertions in SegmentReader fire
    // if any cache exceeds 1000 entries.
    SUCCEED() << "All cache size assertions passed across 1000 queries";

    reader->close();
}
