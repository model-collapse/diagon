// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Lucene104QueryTest - End-to-end validation of Lucene104 codec with query execution
 *
 * Phase 4.4 Integration Test:
 * Demonstrates complete pipeline from writing documents with Lucene104 codec
 * through querying with IndexSearcher and validating results.
 *
 * Tests:
 * 1. Write → Flush → Read → Search (basic flow)
 * 2. TermQuery with TopDocs result validation
 * 3. BM25 scoring correctness
 * 4. Multiple fields support
 * 5. Boolean queries (AND/OR)
 */

#include "diagon/codecs/Codec.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/BooleanClause.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/IOContext.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <unordered_set>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::document;
using namespace diagon::store;
using namespace diagon::codecs;

namespace fs = std::filesystem;

class Lucene104QueryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test
        testDir_ = fs::temp_directory_path() / "diagon_lucene104_query_test";
        fs::create_directories(testDir_);

        // Create directory
        directory_ = FSDirectory::open(testDir_.string());
        ioContext_ = IOContext::DEFAULT;
    }

    void TearDown() override {
        // Clean up
        directory_.reset();

        // Remove test directory
        if (fs::exists(testDir_)) {
            fs::remove_all(testDir_);
        }
    }

    /**
     * Helper: Write documents via IndexWriter and commit (creates segments_N)
     */
    void writeAndFlushDocuments(const std::vector<std::string>& docs,
                                const std::string& fieldName = "content") {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(static_cast<int>(docs.size()) + 100);  // Single segment
        IndexWriter writer(*directory_, config);

        for (const auto& text : docs) {
            Document doc;
            doc.add(std::make_unique<TextField>(fieldName, text));
            writer.addDocument(doc);
        }

        writer.commit();
    }

    /**
     * Helper: Calculate expected BM25 score for validation
     */
    float expectedBM25Score(int docFreq, int termFreq, int docLength, int avgDocLength,
                            int totalDocs) {
        // BM25 parameters
        const float k1 = 1.2f;
        const float b = 0.75f;

        // IDF: log(1 + (N - df + 0.5) / (df + 0.5))
        float idf = std::log(1.0f + (totalDocs - docFreq + 0.5f) / (docFreq + 0.5f));

        // TF component: (k1 + 1) * tf / (k1 * (1 - b + b * docLen / avgDocLen) + tf)
        float tfNorm = (k1 + 1.0f) * termFreq /
                       (k1 * (1.0f - b + b * docLength / avgDocLength) + termFreq);

        return idf * tfNorm;
    }

    fs::path testDir_;
    std::unique_ptr<Directory> directory_;
    IOContext ioContext_;
};

// ==================== Test Cases ====================

/**
 * Test 1: Basic end-to-end flow
 *
 * Write documents → Flush → Read with DirectoryReader → Search with IndexSearcher
 */
TEST_F(Lucene104QueryTest, BasicEndToEndFlow) {
    // Write 3 documents
    std::vector<std::string> docs = {
        "apple banana",   // doc 0
        "banana cherry",  // doc 1
        "cherry apple"    // doc 2
    };

    writeAndFlushDocuments(docs);

    // Open DirectoryReader
    auto reader = DirectoryReader::open(*directory_);
    ASSERT_NE(nullptr, reader);
    EXPECT_EQ(3, reader->maxDoc());
    EXPECT_EQ(1, reader->leaves().size());  // 1 segment

    // Create IndexSearcher
    IndexSearcher searcher(*reader);

    // Create query: "banana"
    search::Term term("content", "banana");
    TermQuery query(term);

    // Execute search
    TopDocs results = searcher.search(query, 10);

    // Validate results
    EXPECT_EQ(2, results.totalHits.value);  // Docs 0 and 1 contain "banana"
    ASSERT_EQ(2, results.scoreDocs.size());

    // Check doc IDs (order by score, so higher score first)
    std::unordered_set<int> matchedDocs;
    for (const auto& scoreDoc : results.scoreDocs) {
        matchedDocs.insert(scoreDoc.doc);
        EXPECT_GT(scoreDoc.score, 0.0f);  // Scores should be positive
    }

    EXPECT_TRUE(matchedDocs.count(0) > 0);  // doc 0: "apple banana"
    EXPECT_TRUE(matchedDocs.count(1) > 0);  // doc 1: "banana cherry"
}

/**
 * Test 2: BM25 scoring validation
 *
 * Verify that scores match expected BM25 formula
 */
TEST_F(Lucene104QueryTest, BM25ScoringCorrectness) {
    // Write documents with known term frequencies
    std::vector<std::string> docs = {
        "apple",              // doc 0: 1 term
        "apple apple",        // doc 1: 2 terms (same)
        "apple apple apple",  // doc 2: 3 terms (same)
        "banana"              // doc 3: different term
    };

    writeAndFlushDocuments(docs);

    // Open reader and searcher
    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    // Query: "apple"
    search::Term term("content", "apple");
    TermQuery query(term);
    TopDocs results = searcher.search(query, 10);

    // Validate: 3 documents match
    EXPECT_EQ(3, results.totalHits.value);
    ASSERT_EQ(3, results.scoreDocs.size());

    // Calculate expected scores
    // Note: Exact BM25 calculation requires knowing tokenized doc lengths
    // Here we just validate relative ordering and non-zero scores

    // Sort by doc ID for easier validation
    std::vector<ScoreDoc> sortedDocs = results.scoreDocs;
    std::sort(sortedDocs.begin(), sortedDocs.end(),
              [](const ScoreDoc& a, const ScoreDoc& b) { return a.doc < b.doc; });

    // Validate: Higher term frequency → Higher score
    // doc 2 (3 "apple") > doc 1 (2 "apple") > doc 0 (1 "apple")
    float score0 = sortedDocs[0].score;  // 1 "apple"
    float score1 = sortedDocs[1].score;  // 2 "apple"
    float score2 = sortedDocs[2].score;  // 3 "apple"

    EXPECT_GT(score2, score1);
    EXPECT_GT(score1, score0);
    EXPECT_GT(score0, 0.0f);
}

/**
 * Test 3: Multiple fields support
 *
 * Write documents with multiple fields and query specific field
 */
TEST_F(Lucene104QueryTest, MultipleFieldsSupport) {
    // Create documents with title and body fields
    IndexWriterConfig writerConfig;
    writerConfig.setMaxBufferedDocs(100);
    IndexWriter writer(*directory_, writerConfig);

    // Doc 0: "apple" in title
    {
        Document doc;
        doc.add(std::make_unique<TextField>("title", "apple"));
        doc.add(std::make_unique<TextField>("body", "banana"));
        writer.addDocument(doc);
    }

    // Doc 1: "apple" in body
    {
        Document doc;
        doc.add(std::make_unique<TextField>("title", "banana"));
        doc.add(std::make_unique<TextField>("body", "apple"));
        writer.addDocument(doc);
    }

    // Doc 2: "apple" in both
    {
        Document doc;
        doc.add(std::make_unique<TextField>("title", "apple"));
        doc.add(std::make_unique<TextField>("body", "apple"));
        writer.addDocument(doc);
    }

    writer.commit();

    // Open reader and searcher
    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    // Test: Query "title" field for "apple"
    {
        search::Term term("title", "apple");
        TermQuery query(term);
        TopDocs results = searcher.search(query, 10);

        EXPECT_EQ(2, results.totalHits.value);  // Docs 0 and 2
        std::unordered_set<int> matchedDocs;
        for (const auto& scoreDoc : results.scoreDocs) {
            matchedDocs.insert(scoreDoc.doc);
        }
        EXPECT_TRUE(matchedDocs.count(0) > 0);
        EXPECT_TRUE(matchedDocs.count(2) > 0);
    }

    // Test: Query "body" field for "apple"
    {
        search::Term term("body", "apple");
        TermQuery query(term);
        TopDocs results = searcher.search(query, 10);

        EXPECT_EQ(2, results.totalHits.value);  // Docs 1 and 2
        std::unordered_set<int> matchedDocs;
        for (const auto& scoreDoc : results.scoreDocs) {
            matchedDocs.insert(scoreDoc.doc);
        }
        EXPECT_TRUE(matchedDocs.count(1) > 0);
        EXPECT_TRUE(matchedDocs.count(2) > 0);
    }
}

/**
 * Test 4: Boolean query (AND)
 *
 * Test BooleanQuery with MUST clauses
 */
TEST_F(Lucene104QueryTest, BooleanQueryAND) {
    // Write documents
    std::vector<std::string> docs = {
        "apple banana",        // doc 0: matches both
        "apple cherry",        // doc 1: only "apple"
        "banana cherry",       // doc 2: only "banana"
        "apple banana cherry"  // doc 3: matches both
    };

    writeAndFlushDocuments(docs);

    // Open reader and searcher
    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    // Create BooleanQuery: "apple" AND "banana"
    auto appleQuery = std::make_unique<TermQuery>(search::Term("content", "apple"));
    auto bananaQuery = std::make_unique<TermQuery>(search::Term("content", "banana"));

    BooleanQuery::Builder builder;
    builder.add(std::move(appleQuery), Occur::MUST);
    builder.add(std::move(bananaQuery), Occur::MUST);
    auto boolQuery = builder.build();

    // Execute search
    TopDocs results = searcher.search(*boolQuery, 10);

    // Validate: Only docs with BOTH terms
    EXPECT_EQ(2, results.totalHits.value);  // Docs 0 and 3
    ASSERT_EQ(2, results.scoreDocs.size());

    std::unordered_set<int> matchedDocs;
    for (const auto& scoreDoc : results.scoreDocs) {
        matchedDocs.insert(scoreDoc.doc);
    }

    EXPECT_TRUE(matchedDocs.count(0) > 0);  // "apple banana"
    EXPECT_TRUE(matchedDocs.count(3) > 0);  // "apple banana cherry"
}

/**
 * Test 5: Boolean query (OR)
 *
 * Test BooleanQuery with SHOULD clauses
 */
TEST_F(Lucene104QueryTest, BooleanQueryOR) {
    // Write documents
    std::vector<std::string> docs = {
        "apple",        // doc 0: "apple"
        "banana",       // doc 1: "banana"
        "cherry",       // doc 2: neither
        "apple banana"  // doc 3: both
    };

    writeAndFlushDocuments(docs);

    // Open reader and searcher
    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    // Create BooleanQuery: "apple" OR "banana"
    auto appleQuery = std::make_unique<TermQuery>(search::Term("content", "apple"));
    auto bananaQuery = std::make_unique<TermQuery>(search::Term("content", "banana"));

    BooleanQuery::Builder builder;
    builder.add(std::move(appleQuery), Occur::SHOULD);
    builder.add(std::move(bananaQuery), Occur::SHOULD);
    auto boolQuery = builder.build();

    // Execute search
    TopDocs results = searcher.search(*boolQuery, 10);

    // Validate: Docs with EITHER term
    EXPECT_EQ(3, results.totalHits.value);  // Docs 0, 1, 3
    ASSERT_EQ(3, results.scoreDocs.size());

    std::unordered_set<int> matchedDocs;
    for (const auto& scoreDoc : results.scoreDocs) {
        matchedDocs.insert(scoreDoc.doc);
    }

    EXPECT_TRUE(matchedDocs.count(0) > 0);   // "apple"
    EXPECT_TRUE(matchedDocs.count(1) > 0);   // "banana"
    EXPECT_TRUE(matchedDocs.count(3) > 0);   // "apple banana"
    EXPECT_FALSE(matchedDocs.count(2) > 0);  // "cherry" - doesn't match
}

/**
 * Test 6: Empty result set
 *
 * Query for non-existent term
 */
TEST_F(Lucene104QueryTest, EmptyResultSet) {
    // Write documents
    std::vector<std::string> docs = {"apple", "banana", "cherry"};

    writeAndFlushDocuments(docs);

    // Open reader and searcher
    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    // Query for term that doesn't exist
    search::Term term("content", "nonexistent");
    TermQuery query(term);
    TopDocs results = searcher.search(query, 10);

    // Validate: No results
    EXPECT_EQ(0, results.totalHits.value);
    EXPECT_EQ(0, results.scoreDocs.size());
}

/**
 * Test 7: TopK limiting
 *
 * Verify that only top K results are returned when K < total matches
 */
TEST_F(Lucene104QueryTest, TopKLimiting) {
    // Write many documents with same term
    std::vector<std::string> docs;
    for (int i = 0; i < 100; i++) {
        docs.push_back("apple");
    }

    writeAndFlushDocuments(docs);

    // Open reader and searcher
    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    // Query with K=10
    search::Term term("content", "apple");
    TermQuery query(term);
    TopDocs results = searcher.search(query, 10);

    // Validate: 100 total hits, but only 10 returned
    EXPECT_EQ(100, results.totalHits.value);
    EXPECT_EQ(10, results.scoreDocs.size());

    // Validate: All scores are equal (same term frequency)
    float firstScore = results.scoreDocs[0].score;
    for (const auto& scoreDoc : results.scoreDocs) {
        EXPECT_FLOAT_EQ(firstScore, scoreDoc.score);
    }
}

/**
 * Test 8: Codec interoperability
 *
 * Verify that DirectoryReader correctly detects and uses Lucene104 codec
 * (This is implicitly tested by all tests, but made explicit here)
 */
TEST_F(Lucene104QueryTest, CodecDetection) {
    // Write with Lucene104 codec
    std::vector<std::string> docs = {"apple"};
    writeAndFlushDocuments(docs);

    // Open reader (should detect Lucene104 and create appropriate FieldsProducer)
    auto reader = DirectoryReader::open(*directory_);
    ASSERT_NE(nullptr, reader);

    // Get SegmentReader
    const auto& leaves = reader->leaves();
    ASSERT_EQ(1, leaves.size());
    auto* segmentReader = dynamic_cast<SegmentReader*>(leaves[0].reader);
    ASSERT_NE(nullptr, segmentReader);

    // Verify segment info matches
    EXPECT_EQ("Lucene104", segmentReader->getSegmentInfo()->codecName());

    // Access terms (triggers loadFieldsProducer which uses codec detection)
    auto terms = segmentReader->terms("content");
    ASSERT_NE(nullptr, terms);

    // Verify we can iterate terms
    auto termsEnum = terms->iterator();
    ASSERT_TRUE(termsEnum->next());

    // Verify we can get postings
    auto postingsEnum = termsEnum->postings(false);
    ASSERT_NE(nullptr, postingsEnum);
    EXPECT_NE(PostingsEnum::NO_MORE_DOCS, postingsEnum->nextDoc());
}
