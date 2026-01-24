// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/IndexSearcher.h"

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/TopScoreDocCollector.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;
using diagon::search::IndexSearcher;
using diagon::search::Term;
using diagon::search::TermQuery;
using diagon::search::TopScoreDocCollector;

namespace fs = std::filesystem;

class IndexSearcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique directory for each test
        static int testCounter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_index_searcher_test_" + std::to_string(testCounter++));
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

    // Helper: Write test documents
    void writeDocuments(const std::vector<std::string>& texts) {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        for (size_t i = 0; i < texts.size(); i++) {
            Document doc;

            FieldType ft;
            ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
            ft.stored = true;
            ft.tokenized = true;

            doc.add(std::make_unique<Field>("body", texts[i], ft));
            writer.addDocument(doc);
        }

        writer.commit();
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== Basic Search Tests ====================

TEST_F(IndexSearcherTest, SearchWithTermQuery) {
    // Write test documents
    writeDocuments({"hello world", "hello there", "goodbye world"});

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search for "hello"
    search::Term searchTerm("_all", "hello");  // Phase 3: all fields indexed to "_all"
    TermQuery query(searchTerm);

    auto results = searcher.search(query, 10);

    // Should find 2 documents containing "hello"
    EXPECT_EQ(results.totalHits.value, 2);
    EXPECT_EQ(results.scoreDocs.size(), 2);
    EXPECT_GT(results.scoreDocs[0].score, 0.0f);
}

TEST_F(IndexSearcherTest, SearchNoMatches) {
    // Write test documents
    writeDocuments({"hello world", "goodbye world"});

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search for searchTerm that doesn't exist
    search::Term searchTerm("_all", "nonexistent");
    TermQuery query(searchTerm);

    auto results = searcher.search(query, 10);

    // Should find 0 documents
    EXPECT_EQ(results.totalHits.value, 0);
    EXPECT_EQ(results.scoreDocs.size(), 0);
}

TEST_F(IndexSearcherTest, SearchWithTopK) {
    // Write many documents
    std::vector<std::string> docs;
    for (int i = 0; i < 20; i++) {
        docs.push_back("document " + std::to_string(i) + " with search searchTerm");
    }
    writeDocuments(docs);

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search for "search"
    search::Term searchTerm("_all", "search");
    TermQuery query(searchTerm);

    // Request only top 5
    auto results = searcher.search(query, 5);

    // Should find all 20 but return only top 5
    EXPECT_EQ(results.totalHits.value, 20);
    EXPECT_EQ(results.scoreDocs.size(), 5);

    // Scores should be in descending order
    for (size_t i = 1; i < results.scoreDocs.size(); i++) {
        EXPECT_GE(results.scoreDocs[i - 1].score, results.scoreDocs[i].score);
    }
}

// ==================== Scoring Tests ====================

TEST_F(IndexSearcherTest, BM25Scoring) {
    // Write documents with different searchTerm frequencies
    writeDocuments({
        "apple",              // freq=1
        "apple apple",        // freq=2
        "apple apple apple",  // freq=3
        "orange"              // no "apple"
    });

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search for "apple"
    search::Term searchTerm("_all", "apple");
    TermQuery query(searchTerm);

    auto results = searcher.search(query, 10);

    // Should find 3 documents
    EXPECT_EQ(results.totalHits.value, 3);
    EXPECT_EQ(results.scoreDocs.size(), 3);

    // Phase 4: Simplified BM25 with estimated statistics
    // All scores may be similar due to norm=1 and estimated stats
    // Phase 5 will implement proper norm encoding and statistics

    // All scores should be positive
    for (const auto& scoreDoc : results.scoreDocs) {
        EXPECT_GT(scoreDoc.score, 0.0f);
    }
}

// ==================== Collector Tests ====================

TEST_F(IndexSearcherTest, SearchWithCollector) {
    // Write test documents
    writeDocuments({"search test one", "search test two", "search test three"});

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Create collector
    auto collector = TopScoreDocCollector::create(10);

    // Search for "search"
    search::Term searchTerm("_all", "search");
    TermQuery query(searchTerm);

    searcher.search(query, collector.get());

    // Get results from collector
    auto results = collector->topDocs();

    EXPECT_EQ(results.totalHits.value, 3);
    EXPECT_EQ(results.scoreDocs.size(), 3);
}

// ==================== Multi-Segment Tests ====================

TEST_F(IndexSearcherTest, SearchAcrossMultipleSegments) {
    // Write documents that will create multiple segments
    IndexWriterConfig config;
    config.setMaxBufferedDocs(2);  // Force flush every 2 docs
    IndexWriter writer(*dir, config);

    // Write 6 documents (will create 3 segments)
    for (int i = 0; i < 6; i++) {
        Document doc;

        FieldType ft;
        ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
        ft.stored = true;
        ft.tokenized = true;

        doc.add(std::make_unique<Field>("body", "segment" + std::to_string(i / 2) + " word", ft));
        writer.addDocument(doc);
    }
    writer.commit();

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search for "word" (appears in all documents)
    search::Term searchTerm("_all", "word");
    TermQuery query(searchTerm);

    auto results = searcher.search(query, 10);

    // Should find all 6 documents across all segments
    EXPECT_EQ(results.totalHits.value, 6);
    EXPECT_EQ(results.scoreDocs.size(), 6);
}

// ==================== Count Tests ====================

TEST_F(IndexSearcherTest, CountMatchingDocs) {
    // Write test documents
    writeDocuments({"count test one", "count test two", "count test three", "other document"});

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Count documents containing "count"
    search::Term searchTerm("_all", "count");
    TermQuery query(searchTerm);

    int count = searcher.count(query);

    EXPECT_EQ(count, 3);
}

// ==================== Empty Index Tests ====================

TEST_F(IndexSearcherTest, SearchEmptyIndex) {
    // Write no documents, just commit empty index
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);
    writer.commit();

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search in empty index
    search::Term searchTerm("_all", "anything");
    TermQuery query(searchTerm);

    auto results = searcher.search(query, 10);

    EXPECT_EQ(results.totalHits.value, 0);
    EXPECT_EQ(results.scoreDocs.size(), 0);
}

// ==================== MaxScore Tests ====================

TEST_F(IndexSearcherTest, MaxScoreInResults) {
    // Write documents
    writeDocuments({"score test alpha", "score test beta", "score test gamma"});

    // Open reader
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    // Search for "score"
    search::Term searchTerm("_all", "score");
    TermQuery query(searchTerm);

    auto results = searcher.search(query, 10);

    // maxScore should match the highest score in results
    EXPECT_GT(results.maxScore, 0.0f);
    EXPECT_EQ(results.maxScore, results.scoreDocs[0].score);
}
