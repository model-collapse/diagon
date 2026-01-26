// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;
using namespace diagon::search;

namespace fs = std::filesystem;

/**
 * Basic end-to-end integration test
 *
 * Tests: Index → Commit → Search → Results
 */
class BasicEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_basic_e2e_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override {
        fs::remove_all(testDir_);
    }

    fs::path testDir_;
};

/**
 * Test: Index 100 documents and search
 */
TEST_F(BasicEndToEndTest, IndexAndSearch100Docs) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    // Index 100 documents
    {
        IndexWriter writer(*dir, config);

        for (int i = 0; i < 100; i++) {
            Document doc;
            std::string content = "document number " + std::to_string(i);
            if (i % 10 == 0) {
                content += " milestone";
            }
            doc.add(std::make_unique<TextField>("content", content));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Search for "milestone"
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(100, reader->maxDoc());

        IndexSearcher searcher(*reader);
        search::Term term("content", "milestone");
        TermQuery query(term);
        auto results = searcher.search(query, 20);

        EXPECT_EQ(10, results.totalHits.value) << "Should find 10 documents with 'milestone'";

        for (const auto& scoreDoc : results.scoreDocs) {
            EXPECT_GT(scoreDoc.score, 0.0f);
        }
    }
}

/**
 * Test: Performance - index 10K docs, measure throughput
 */
TEST_F(BasicEndToEndTest, IndexingPerformance) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    const int NUM_DOCS = 10000;

    auto startTime = std::chrono::high_resolution_clock::now();

    {
        IndexWriter writer(*dir, config);

        for (int i = 0; i < NUM_DOCS; i++) {
            Document doc;
            std::string content = "Document " + std::to_string(i) +
                                 " with some content to index";
            doc.add(std::make_unique<TextField>("title", content));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    double docsPerSecond = (NUM_DOCS * 1000.0) / duration.count();

    std::cout << "\n";
    std::cout << "=== Indexing Performance ===\n";
    std::cout << "Documents indexed: " << NUM_DOCS << "\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "Throughput: " << static_cast<int>(docsPerSecond) << " docs/sec\n";
    std::cout << "===========================\n";

    // Search performance
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(NUM_DOCS, reader->maxDoc());

        IndexSearcher searcher(*reader);
        search::Term term("title", "Document");
        TermQuery query(term);

        auto searchStart = std::chrono::high_resolution_clock::now();
        auto results = searcher.search(query, 10);
        auto searchEnd = std::chrono::high_resolution_clock::now();

        auto searchDuration = std::chrono::duration_cast<std::chrono::microseconds>(searchEnd - searchStart);

        std::cout << "\n";
        std::cout << "=== Search Performance ===\n";
        std::cout << "Query: 'Document' (matches all " << NUM_DOCS << " docs)\n";
        std::cout << "Search latency: " << searchDuration.count() << " μs\n";
        std::cout << "Results returned: " << results.scoreDocs.size() << "\n";
        std::cout << "========================\n\n";

        EXPECT_EQ(NUM_DOCS, results.totalHits.value);
        EXPECT_EQ(10, results.scoreDocs.size());
    }

    // Sanity check: should index at least 1000 docs/sec
    EXPECT_GT(docsPerSecond, 1000.0) << "Indexing throughput too low";
}

/**
 * Test: BM25 scoring with length normalization
 */
TEST_F(BasicEndToEndTest, BM25ScoringWithNorms) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        // Short doc (1 term)
        Document doc0;
        doc0.add(std::make_unique<TextField>("body", "query"));
        writer.addDocument(doc0);

        // Medium doc (5 terms)
        Document doc1;
        doc1.add(std::make_unique<TextField>("body", "query apple banana cherry date"));
        writer.addDocument(doc1);

        // Long doc (10 terms)
        Document doc2;
        doc2.add(std::make_unique<TextField>("body", "query apple banana cherry date elderberry fig grape honeydew "));
        writer.addDocument(doc2);

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        search::Term term("body", "query");
        TermQuery query(term);
        auto results = searcher.search(query, 10);

        ASSERT_EQ(3, results.totalHits.value);
        ASSERT_EQ(3, results.scoreDocs.size());

        // Verify shorter docs rank higher (BM25 length normalization)
        EXPECT_EQ(0, results.scoreDocs[0].doc) << "Shortest doc should rank first";
        EXPECT_EQ(1, results.scoreDocs[1].doc) << "Medium doc should rank second";
        EXPECT_EQ(2, results.scoreDocs[2].doc) << "Longest doc should rank third";

        // Verify scores decrease
        EXPECT_GT(results.scoreDocs[0].score, results.scoreDocs[1].score);
        EXPECT_GT(results.scoreDocs[1].score, results.scoreDocs[2].score);

        std::cout << "\n";
        std::cout << "=== BM25 Length Normalization ===\n";
        std::cout << "Doc 0 (short):  score = " << results.scoreDocs[0].score << "\n";
        std::cout << "Doc 1 (medium): score = " << results.scoreDocs[1].score << "\n";
        std::cout << "Doc 2 (long):   score = " << results.scoreDocs[2].score << "\n";
        std::cout << "=================================\n\n";
    }
}

/**
 * Test: Multiple segments
 */
TEST_F(BasicEndToEndTest, MultipleSegments) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);  // Force segment every 10 docs

    {
        IndexWriter writer(*dir, config);

        // Add 50 documents - creates 5 segments
        for (int i = 0; i < 50; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "test document " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();

        std::cout << "\n";
        std::cout << "=== Multi-Segment Index ===\n";
        std::cout << "Number of segments: " << leaves.size() << "\n";
        std::cout << "Total documents: " << reader->maxDoc() << "\n";
        std::cout << "===========================\n\n";

        EXPECT_GE(leaves.size(), 1);
        EXPECT_LE(leaves.size(), 5);

        // Search across all segments
        IndexSearcher searcher(*reader);
        search::Term term("content", "test");
        TermQuery query(term);
        auto results = searcher.search(query, 100);

        EXPECT_EQ(50, results.totalHits.value);
    }
}
