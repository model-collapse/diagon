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

#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;
using namespace diagon::search;

namespace fs = std::filesystem;

/**
 * End-to-end integration test for BM25 scoring with norms
 *
 * Tests that:
 * 1. TermQuery properly uses norms from segments
 * 2. Shorter documents get higher BM25 scores (due to length normalization)
 * 3. Complete pipeline: IndexWriter → norms → SegmentReader → TermQuery → BM25 scores
 */
class BM25NormsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_bm25_norms_integration_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override { fs::remove_all(testDir_); }

    fs::path testDir_;
};

/**
 * Test that shorter documents containing a term get higher BM25 scores
 * than longer documents containing the same term.
 *
 * This verifies that norms (document length normalization) are working correctly.
 */
TEST_F(BM25NormsIntegrationTest, ShorterDocsGetHigherScores) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    // Write documents with varying lengths, all containing "target"
    {
        IndexWriter writer(*dir, config);

        // Document 0: Very short (1 word)
        Document doc0;
        doc0.add(std::make_unique<TextField>("content", "target"));
        writer.addDocument(doc0);

        // Document 1: Short (4 words)
        Document doc1;
        doc1.add(std::make_unique<TextField>("content", "target one two three"));
        writer.addDocument(doc1);

        // Document 2: Medium (9 words)
        Document doc2;
        doc2.add(std::make_unique<TextField>("content",
                                             "target one two three four five six seven eight"));
        writer.addDocument(doc2);

        // Document 3: Long (16 words)
        Document doc3;
        doc3.add(std::make_unique<TextField>("content",
                                             "target one two three four five six seven eight nine "
                                             "ten eleven twelve thirteen fourteen fifteen"));
        writer.addDocument(doc3);

        writer.commit();
        writer.close();
    }

    // Search for "target" and verify score ordering
    {
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        search::Term searchTerm("content", "target");
        TermQuery query(searchTerm);

        auto results = searcher.search(query, 10);

        // All 4 documents should match
        ASSERT_EQ(4, results.totalHits.value) << "All documents contain 'target'";
        ASSERT_EQ(4, results.scoreDocs.size());

        // Verify scores are positive
        for (const auto& scoreDoc : results.scoreDocs) {
            EXPECT_GT(scoreDoc.score, 0.0f)
                << "Doc " << scoreDoc.doc << " should have positive score";
        }

        // Verify score ordering: shorter docs should have higher scores
        // Results are already sorted by score (descending)
        EXPECT_EQ(0, results.scoreDocs[0].doc) << "Shortest doc (1 word) should rank first";
        EXPECT_EQ(1, results.scoreDocs[1].doc) << "Short doc (4 words) should rank second";
        EXPECT_EQ(2, results.scoreDocs[2].doc) << "Medium doc (9 words) should rank third";
        EXPECT_EQ(3, results.scoreDocs[3].doc) << "Long doc (16 words) should rank fourth";

        // Verify scores decrease as document length increases
        EXPECT_GT(results.scoreDocs[0].score, results.scoreDocs[1].score)
            << "Shorter doc should score higher";
        EXPECT_GT(results.scoreDocs[1].score, results.scoreDocs[2].score)
            << "Shorter doc should score higher";
        EXPECT_GT(results.scoreDocs[2].score, results.scoreDocs[3].score)
            << "Shorter doc should score higher";

        // Verify the score difference is significant (not just rounding)
        float scoreDrop = results.scoreDocs[0].score - results.scoreDocs[3].score;
        EXPECT_GT(scoreDrop, 0.1f)
            << "Score difference between shortest and longest should be significant";
    }
}

/**
 * Test that term frequency and document length both affect BM25 scores.
 *
 * Verifies that:
 * - Higher term frequency → higher score
 * - Shorter document → higher score
 * - The combination works correctly
 */
TEST_F(BM25NormsIntegrationTest, TermFrequencyAndLengthNormalization) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        // Document 0: Term appears once in short doc
        Document doc0;
        doc0.add(std::make_unique<TextField>("content", "apple orange"));
        writer.addDocument(doc0);

        // Document 1: Term appears many times in medium doc
        Document doc1;
        doc1.add(std::make_unique<TextField>("content", "apple apple apple orange banana"));
        writer.addDocument(doc1);

        // Document 2: Term appears once in long doc
        Document doc2;
        doc2.add(std::make_unique<TextField>("content",
                                             "apple orange banana kiwi mango grape peach plum"));
        writer.addDocument(doc2);

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        search::Term searchTerm("content", "apple");
        TermQuery query(searchTerm);

        auto results = searcher.search(query, 10);

        ASSERT_EQ(3, results.totalHits.value);
        ASSERT_EQ(3, results.scoreDocs.size());

        // All documents should have positive scores
        for (const auto& scoreDoc : results.scoreDocs) {
            EXPECT_GT(scoreDoc.score, 0.0f)
                << "Doc " << scoreDoc.doc << " should have positive score";
        }

        // Verify that the short doc (2 terms) ranks higher than the long doc (8 terms)
        // Both have tf=1, so length normalization should favor the short doc
        int shortDocRank = -1, longDocRank = -1;
        for (size_t i = 0; i < results.scoreDocs.size(); i++) {
            if (results.scoreDocs[i].doc == 0)
                shortDocRank = i;
            if (results.scoreDocs[i].doc == 2)
                longDocRank = i;
        }
        EXPECT_LT(shortDocRank, longDocRank)
            << "Short doc (2 terms, tf=1) should rank higher than long doc (8 terms, tf=1)";

        // The long doc should rank lowest
        EXPECT_EQ(2, results.scoreDocs[2].doc) << "Long doc with tf=1 should rank lowest";
    }
}

/**
 * Test that norms work correctly across multiple segments
 */
TEST_F(BM25NormsIntegrationTest, NormsAcrossMultipleSegments) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(2);  // Force multiple segments

    {
        IndexWriter writer(*dir, config);

        // Create 6 documents (will create 3 segments)
        for (int i = 0; i < 6; i++) {
            Document doc;
            std::string content = "search";
            // Add padding to make docs of different lengths
            for (int j = 0; j < i; j++) {
                content += " word";
            }
            doc.add(std::make_unique<TextField>("content", content));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_GT(reader->leaves().size(), 1) << "Should have multiple segments";

        IndexSearcher searcher(*reader);

        search::Term searchTerm("content", "search");
        TermQuery query(searchTerm);

        auto results = searcher.search(query, 10);

        ASSERT_EQ(6, results.totalHits.value);
        ASSERT_EQ(6, results.scoreDocs.size());

        // Verify that shorter docs still get higher scores across segments
        // Doc 0 (shortest) should rank first
        EXPECT_EQ(0, results.scoreDocs[0].doc);

        // Scores should generally decrease
        for (size_t i = 0; i < results.scoreDocs.size() - 1; i++) {
            EXPECT_GE(results.scoreDocs[i].score, results.scoreDocs[i + 1].score)
                << "Scores should not increase as docs get longer";
        }
    }
}

/**
 * Test that missing norms (fields without norms) don't crash
 */
TEST_F(BM25NormsIntegrationTest, HandlesFieldsWithoutNorms) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        // Create field type with norms omitted
        FieldType ftNoNorms;
        ftNoNorms.indexOptions = IndexOptions::DOCS_AND_FREQS;
        ftNoNorms.stored = false;
        ftNoNorms.tokenized = true;
        ftNoNorms.omitNorms = true;  // Disable norms

        Document doc0;
        doc0.add(std::make_unique<Field>("no_norms", "hello world", ftNoNorms));
        writer.addDocument(doc0);

        Document doc1;
        doc1.add(std::make_unique<Field>("no_norms", "hello", ftNoNorms));
        writer.addDocument(doc1);

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        search::Term searchTerm("no_norms", "hello");
        TermQuery query(searchTerm);

        // Should not crash even though norms are missing
        EXPECT_NO_THROW({
            auto results = searcher.search(query, 10);
            EXPECT_EQ(2, results.totalHits.value);
        });
    }
}
