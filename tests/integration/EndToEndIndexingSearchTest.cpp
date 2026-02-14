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
 * Comprehensive end-to-end integration test for the complete indexing and search pipeline.
 *
 * Tests the complete workflow:
 * 1. Index documents with various field types
 * 2. Commit and close writer
 * 3. Open reader and search
 * 4. Verify results and scoring
 * 5. Test updates and deletions
 * 6. Test reader reopening
 */
class EndToEndIndexingSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_end_to_end_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override { fs::remove_all(testDir_); }

    fs::path testDir_;
};

/**
 * Test basic indexing and search workflow
 */
TEST_F(EndToEndIndexingSearchTest, BasicIndexingAndSearch) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    // Phase 1: Index documents
    {
        IndexWriter writer(*dir, config);

        // Add 100 documents with varying content
        for (int i = 0; i < 100; i++) {
            Document doc;

            // Text field with varying content
            std::string content = "document number " + std::to_string(i);
            if (i % 10 == 0) {
                content += " important milestone";
            }
            if (i % 5 == 0) {
                content += " special marker";
            }

            doc.add(std::make_unique<TextField>("title", content));
            doc.add(std::make_unique<TextField>("body", "This is the body of document " +
                                                            std::to_string(i)));

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Phase 2: Search documents
    {
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        // Test 1: Search for "important"
        {
            search::Term term("title", "important");
            TermQuery query(term);
            auto results = searcher.search(query, 20);

            // Should find 10 documents (i % 10 == 0)
            EXPECT_EQ(10, results.totalHits.value) << "Should find 10 documents with 'important'";
            EXPECT_GE(results.scoreDocs.size(), 10);

            // All results should have positive scores
            for (const auto& scoreDoc : results.scoreDocs) {
                EXPECT_GT(scoreDoc.score, 0.0f)
                    << "Doc " << scoreDoc.doc << " should have positive score";
            }
        }

        // Test 2: Search for "special"
        {
            search::Term term("title", "special");
            TermQuery query(term);
            auto results = searcher.search(query, 30);

            // Should find 20 documents (i % 5 == 0)
            EXPECT_EQ(20, results.totalHits.value) << "Should find 20 documents with 'special'";
        }

        // Test 3: Search for "document" (appears in all docs)
        {
            search::Term term("title", "document");
            TermQuery query(term);
            auto results = searcher.search(query, 10);

            // Should find all 100 documents, but only return top 10
            EXPECT_EQ(100, results.totalHits.value) << "Should find all 100 documents";
            EXPECT_EQ(10, results.scoreDocs.size()) << "Should return top 10 results";

            // Results should be sorted by score
            for (size_t i = 1; i < results.scoreDocs.size(); i++) {
                EXPECT_GE(results.scoreDocs[i - 1].score, results.scoreDocs[i].score)
                    << "Results should be sorted by score descending";
            }
        }

        // Test 4: Search for non-existent term
        {
            search::Term term("title", "nonexistent");
            TermQuery query(term);
            auto results = searcher.search(query, 10);

            EXPECT_EQ(0, results.totalHits.value) << "Should find no documents";
            EXPECT_EQ(0, results.scoreDocs.size());
        }
    }
}

/**
 * Test indexing performance with larger dataset
 */
TEST_F(EndToEndIndexingSearchTest, IndexingPerformance) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    const int NUM_DOCS = 10000;

    auto startTime = std::chrono::high_resolution_clock::now();

    {
        IndexWriter writer(*dir, config);

        for (int i = 0; i < NUM_DOCS; i++) {
            Document doc;

            std::string title = "Document " + std::to_string(i);
            std::string body = "This is the content of document number " + std::to_string(i) +
                               " with some additional text to make it more realistic";

            doc.add(std::make_unique<TextField>("title", title));
            doc.add(std::make_unique<TextField>("body", body));

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    double docsPerSecond = (NUM_DOCS * 1000.0) / duration.count();

    std::cout << "Indexed " << NUM_DOCS << " documents in " << duration.count() << " ms"
              << std::endl;
    std::cout << "Throughput: " << static_cast<int>(docsPerSecond) << " docs/sec" << std::endl;

    // Verify we can search the index
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(NUM_DOCS, reader->maxDoc()) << "Reader should see all documents";

        IndexSearcher searcher(*reader);
        search::Term term("title", "Document");
        TermQuery query(term);

        auto searchStart = std::chrono::high_resolution_clock::now();
        auto results = searcher.search(query, 10);
        auto searchEnd = std::chrono::high_resolution_clock::now();

        auto searchDuration = std::chrono::duration_cast<std::chrono::microseconds>(searchEnd -
                                                                                    searchStart);

        std::cout << "Search latency: " << searchDuration.count() << " μs" << std::endl;

        EXPECT_EQ(NUM_DOCS, results.totalHits.value) << "Search should find all documents";
        EXPECT_EQ(10, results.scoreDocs.size()) << "Should return top 10 results";
    }

    // Basic performance expectations (very conservative)
    EXPECT_GT(docsPerSecond, 1000.0) << "Should index at least 1000 docs/sec";
}

/**
 * Test updates and deletions
 */
TEST_F(EndToEndIndexingSearchTest, UpdatesAndDeletions) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    // Initial indexing
    {
        IndexWriter writer(*dir, config);

        for (int i = 0; i < 50; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("id", std::to_string(i)));
            doc.add(std::make_unique<TextField>("content", "initial version " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Verify initial state
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(50, reader->maxDoc());
        EXPECT_EQ(50, reader->numDocs()) << "All documents should be live";
    }

    // Delete some documents
    {
        config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);
        IndexWriter writer(*dir, config);

        // Delete documents with id 10-19
        for (int i = 10; i < 20; i++) {
            search::Term term("id", std::to_string(i));
            writer.deleteDocuments(term);
        }

        writer.commit();
        writer.close();
    }

    // Verify deletions
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(50, reader->maxDoc()) << "maxDoc should still be 50";
        EXPECT_EQ(40, reader->numDocs()) << "Only 40 documents should be live after deletions";

        // Search should only find live documents
        IndexSearcher searcher(*reader);
        search::Term term("content", "version");
        TermQuery query(term);
        auto results = searcher.search(query, 100);

        EXPECT_EQ(40, results.totalHits.value) << "Search should only find 40 live documents";
    }

    // Update documents (delete + add)
    {
        config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);
        IndexWriter writer(*dir, config);

        // Update documents 0-9
        for (int i = 0; i < 10; i++) {
            search::Term term("id", std::to_string(i));
            Document newDoc;
            newDoc.add(std::make_unique<TextField>("id", std::to_string(i)));
            newDoc.add(
                std::make_unique<TextField>("content", "updated version " + std::to_string(i)));
            writer.updateDocument(term, newDoc);
        }

        writer.commit();
        writer.close();
    }

    // Verify updates
    {
        auto reader = DirectoryReader::open(*dir);

        // maxDoc increases (old versions still counted)
        EXPECT_GT(reader->maxDoc(), 50);

        // numDocs should be 40 (10 updated + 30 unchanged)
        EXPECT_EQ(40, reader->numDocs());

        // Search for updated content
        IndexSearcher searcher(*reader);
        search::Term term("content", "updated");
        TermQuery query(term);
        auto results = searcher.search(query, 20);

        EXPECT_EQ(10, results.totalHits.value) << "Should find 10 updated documents";
    }
}

/**
 * Test reader reopening
 */
TEST_F(EndToEndIndexingSearchTest, ReaderReopening) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    // Initial indexing
    {
        IndexWriter writer(*dir, config);
        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch1 doc" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    // Open reader
    auto reader1 = DirectoryReader::open(*dir);
    EXPECT_EQ(10, reader1->numDocs());

    // Add more documents
    {
        config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);
        IndexWriter writer(*dir, config);
        for (int i = 10; i < 20; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content", "batch2 doc" + std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    // Old reader should still see 10 documents
    EXPECT_EQ(10, reader1->numDocs()) << "Old reader should not see new documents";

    // Reopen reader
    auto reader2 = DirectoryReader::openIfChanged(*reader1);
    ASSERT_NE(nullptr, reader2) << "Reader should be reopenable";
    EXPECT_EQ(20, reader2->numDocs()) << "New reader should see all 20 documents";

    // Search on new reader should find documents from both batches
    IndexSearcher searcher(*reader2);

    {
        search::Term term("content", "batch1");
        TermQuery query(term);
        auto results = searcher.search(query, 20);
        EXPECT_EQ(10, results.totalHits.value);
    }

    {
        search::Term term("content", "batch2");
        TermQuery query(term);
        auto results = searcher.search(query, 20);
        EXPECT_EQ(10, results.totalHits.value);
    }
}

/**
 * Test multiple segments
 */
TEST_F(EndToEndIndexingSearchTest, MultipleSegments) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(10);  // Force segment flush every 10 docs

    {
        IndexWriter writer(*dir, config);

        // Add 50 documents - will create 5 segments
        for (int i = 0; i < 50; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("content",
                                                "segment test document " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();

        std::cout << "Number of segments: " << leaves.size() << std::endl;
        EXPECT_GE(leaves.size(), 1) << "Should have at least one segment";
        EXPECT_LE(leaves.size(), 5) << "Should have at most 5 segments";

        // Search should work across all segments
        IndexSearcher searcher(*reader);
        search::Term term("content", "segment");
        TermQuery query(term);
        auto results = searcher.search(query, 100);

        EXPECT_EQ(50, results.totalHits.value) << "Should find all 50 documents across segments";
    }
}

/**
 * Test stored fields retrieval
 */
TEST_F(EndToEndIndexingSearchTest, StoredFieldsRetrieval) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        for (int i = 0; i < 20; i++) {
            Document doc;

            // Create field type with stored=true
            FieldType ft;
            ft.stored = true;
            ft.indexed = true;
            ft.tokenized = true;

            doc.add(std::make_unique<Field>("id", std::to_string(i), ft));
            doc.add(std::make_unique<Field>("title", "Document " + std::to_string(i), ft));

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto& leafContext = leaves[0];

        // Get stored fields reader
        auto* storedFieldsReader =
            dynamic_cast<SegmentReader*>(leafContext.reader)->storedFieldsReader();
        ASSERT_TRUE(storedFieldsReader != nullptr) << "Should have stored fields reader";

        // Read stored fields for document 5
        auto storedDoc = storedFieldsReader->document(5);
        ASSERT_NE(nullptr, storedDoc);

        // Verify stored fields
        auto* idField = storedDoc->getField("id");
        ASSERT_NE(nullptr, idField);
        EXPECT_EQ("5", idField->stringValue());

        auto* titleField = storedDoc->getField("title");
        ASSERT_NE(nullptr, titleField);
        EXPECT_EQ("Document 5", titleField->stringValue());
    }
}
