// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * PhraseQueryTest - End-to-end tests for exact phrase matching (slop=0).
 *
 * Tests the full position pipeline:
 *   Token positions → FreqProxTermsWriter → Lucene104PostingsWriter (.pos file)
 *   → BlockTreeTermsReader → Lucene104PostingsReader → PhraseScorer
 *
 * Test cases:
 * 1. Exact phrase matches documents with consecutive positions
 * 2. Wrong order does NOT match (order matters)
 * 3. Single-term phrase rewrites to TermQuery
 * 4. No matches when terms in different docs
 * 5. Multiple phrase matches per document
 * 6. PhraseQuery Builder API
 * 7. toString / equals / hashCode
 * 8. Empty phrase handling
 */

#include "diagon/search/PhraseQuery.h"

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/Term.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

// ==================== Helper: FieldType with positions ====================

static FieldType textFieldWithPositions() {
    FieldType ft;
    ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    ft.stored = false;
    ft.tokenized = true;
    return ft;
}

// ==================== Test Fixture ====================

class PhraseQueryTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_phrase_query_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
        directory_ = FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        directory_.reset();
        if (fs::exists(testDir_)) {
            fs::remove_all(testDir_);
        }
    }

    /**
     * Index documents with position-enabled field type.
     */
    void indexDocuments(const std::vector<std::string>& docs,
                        const std::string& fieldName = "content") {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(static_cast<int>(docs.size()) + 100);
        IndexWriter writer(*directory_, config);

        FieldType ft = textFieldWithPositions();

        for (const auto& text : docs) {
            Document doc;
            doc.add(std::make_unique<TextField>(fieldName, text, ft));
            writer.addDocument(doc);
        }

        writer.commit();
    }

    /**
     * Search with a phrase query and return hit count.
     */
    int searchPhrase(const std::vector<std::string>& terms,
                     const std::string& fieldName = "content") {
        auto reader = DirectoryReader::open(*directory_);
        IndexSearcher searcher(*reader);

        PhraseQuery::Builder builder(fieldName);
        for (const auto& t : terms) {
            builder.add(t);
        }
        auto query = builder.build();

        auto topDocs = searcher.search(*query, 100);
        return static_cast<int>(topDocs.scoreDocs.size());
    }

    fs::path testDir_;
    std::unique_ptr<Directory> directory_;
};

// ==================== Test Cases ====================

/**
 * Test 1: Exact phrase "quick brown fox" matches a document containing it.
 */
TEST_F(PhraseQueryTest, ExactPhraseMatch) {
    indexDocuments({
        "the quick brown fox jumps over the lazy dog",
        "a brown dog chased the fox",
        "quick and brown are colors",
    });

    // "quick brown fox" should match doc 0 only
    int hits = searchPhrase({"quick", "brown", "fox"});
    EXPECT_EQ(hits, 1) << "Exact phrase 'quick brown fox' should match 1 document";
}

/**
 * Test 2: Wrong order does NOT match. "fox brown quick" is not the same as "quick brown fox".
 */
TEST_F(PhraseQueryTest, WrongOrderNoMatch) {
    indexDocuments({
        "the quick brown fox jumps over the lazy dog",
    });

    // "fox brown quick" should NOT match (wrong order)
    int hits = searchPhrase({"fox", "brown", "quick"});
    EXPECT_EQ(hits, 0) << "Reversed phrase 'fox brown quick' should NOT match";
}

/**
 * Test 3: Two-term phrase matches.
 */
TEST_F(PhraseQueryTest, TwoTermPhrase) {
    indexDocuments({
        "oil price went up today",
        "the price of oil is high",
        "oil and gas price report",
    });

    // "oil price" should match doc 0 only (consecutive)
    int hits = searchPhrase({"oil", "price"});
    EXPECT_EQ(hits, 1) << "'oil price' should match only doc 0";
}

/**
 * Test 4: No matches when terms appear in different documents.
 */
TEST_F(PhraseQueryTest, TermsInDifferentDocs) {
    indexDocuments({
        "the quick fox",
        "the brown dog",
    });

    int hits = searchPhrase({"quick", "brown"});
    EXPECT_EQ(hits, 0) << "'quick brown' should not match when terms are in different docs";
}

/**
 * Test 5: Multiple phrase matches per document - should still return 1 hit (the doc).
 */
TEST_F(PhraseQueryTest, MultiplePhraseMatchesPerDoc) {
    indexDocuments({
        "oil price oil price oil price",
    });

    int hits = searchPhrase({"oil", "price"});
    EXPECT_EQ(hits, 1) << "Document with multiple 'oil price' occurrences should be a single hit";
}

/**
 * Test 6: Multiple documents matching.
 */
TEST_F(PhraseQueryTest, MultipleDocsMatch) {
    indexDocuments({
        "oil price report",
        "oil price analysis",
        "gas price report",
        "oil price forecast",
    });

    int hits = searchPhrase({"oil", "price"});
    EXPECT_EQ(hits, 3) << "'oil price' should match 3 documents";
}

/**
 * Test 7: Single-term phrase rewrites to TermQuery.
 */
TEST_F(PhraseQueryTest, SingleTermRewritesToTermQuery) {
    indexDocuments({"test document with words"});

    auto reader = DirectoryReader::open(*directory_);

    PhraseQuery::Builder builder("content");
    builder.add("test");
    auto phraseQuery = builder.build();

    auto rewritten = phraseQuery->rewrite(*reader);
    ASSERT_NE(rewritten, nullptr);

    // Should be rewritten to TermQuery
    auto* termQuery = dynamic_cast<TermQuery*>(rewritten.get());
    EXPECT_NE(termQuery, nullptr) << "Single-term phrase should rewrite to TermQuery";
}

/**
 * Test 8: PhraseQuery Builder API.
 */
TEST_F(PhraseQueryTest, BuilderAPI) {
    PhraseQuery::Builder builder("content");
    builder.add("hello");
    builder.add("world");
    auto query = builder.build();

    EXPECT_EQ(query->getField(), "content");
    EXPECT_EQ(query->getTerms().size(), 2u);
    // Term::text() returns BytesRef::toString() which is hex-encoded.
    // Compare bytes directly instead.
    auto bytesToStr = [](const util::BytesRef& b) {
        return std::string(reinterpret_cast<const char*>(b.data()), b.length());
    };
    EXPECT_EQ(bytesToStr(query->getTerms()[0].bytes()), "hello");
    EXPECT_EQ(bytesToStr(query->getTerms()[1].bytes()), "world");
    EXPECT_EQ(query->getPositions().size(), 2u);
    EXPECT_EQ(query->getPositions()[0], 0);
    EXPECT_EQ(query->getPositions()[1], 1);
    EXPECT_EQ(query->getSlop(), 0);
}

/**
 * Test 9: Builder with explicit positions.
 */
TEST_F(PhraseQueryTest, BuilderExplicitPositions) {
    PhraseQuery::Builder builder("content");
    builder.add("hello", 0);
    builder.add("world", 2);  // Gap at position 1 (e.g., stopword removed)
    auto query = builder.build();

    EXPECT_EQ(query->getPositions()[0], 0);
    EXPECT_EQ(query->getPositions()[1], 2);
}

/**
 * Test 10: toString formatting.
 */
TEST_F(PhraseQueryTest, ToStringFormatting) {
    PhraseQuery::Builder builder("content");
    builder.add("quick");
    builder.add("brown");
    builder.add("fox");
    auto query = builder.build();

    // Same field - no prefix
    EXPECT_EQ(query->toString("content"), "\"quick brown fox\"");

    // Different field - prefix with field name
    EXPECT_EQ(query->toString("title"), "content:\"quick brown fox\"");
}

/**
 * Test 11: equals and hashCode.
 */
TEST_F(PhraseQueryTest, EqualsAndHashCode) {
    auto makeQuery = [](const std::string& field, const std::vector<std::string>& terms) {
        PhraseQuery::Builder builder(field);
        for (const auto& t : terms) {
            builder.add(t);
        }
        return builder.build();
    };

    auto q1 = makeQuery("content", {"quick", "brown", "fox"});
    auto q2 = makeQuery("content", {"quick", "brown", "fox"});
    auto q3 = makeQuery("content", {"quick", "brown"});
    auto q4 = makeQuery("title", {"quick", "brown", "fox"});

    // Same queries are equal
    EXPECT_TRUE(q1->equals(*q2));
    EXPECT_EQ(q1->hashCode(), q2->hashCode());

    // Different term count
    EXPECT_FALSE(q1->equals(*q3));

    // Different field
    EXPECT_FALSE(q1->equals(*q4));
}

/**
 * Test 12: clone produces equal query.
 */
TEST_F(PhraseQueryTest, CloneProducesEqual) {
    PhraseQuery::Builder builder("content");
    builder.add("hello");
    builder.add("world");
    auto query = builder.build();

    auto cloned = query->clone();
    EXPECT_TRUE(query->equals(*cloned));
    EXPECT_EQ(query->hashCode(), cloned->hashCode());
}

/**
 * Test 13: Terms not adjacent should NOT match.
 * "quick ... fox" with a word in between should not match "quick fox" phrase.
 */
TEST_F(PhraseQueryTest, NonAdjacentTermsNoMatch) {
    indexDocuments({
        "quick brown fox",  // "quick" at 0, "brown" at 1, "fox" at 2
    });

    // "quick fox" requires positions 0,1 but fox is at position 2
    int hits = searchPhrase({"quick", "fox"});
    EXPECT_EQ(hits, 0) << "'quick fox' should not match 'quick brown fox' (not adjacent)";
}

/**
 * Test 14: Phrase scoring - document with more phrase matches should score higher.
 */
TEST_F(PhraseQueryTest, PhraseScoring) {
    indexDocuments({
        "oil price oil price oil price",  // 3 matches
        "oil price report today",         // 1 match
    });

    auto reader = DirectoryReader::open(*directory_);
    IndexSearcher searcher(*reader);

    PhraseQuery::Builder builder("content");
    builder.add("oil");
    builder.add("price");
    auto query = builder.build();

    auto topDocs = searcher.search(*query, 10);
    ASSERT_EQ(topDocs.scoreDocs.size(), 2u);

    // Doc with more phrase matches should score higher
    // (may be doc 0 or doc 1 depending on BM25 normalization, but both should appear)
    EXPECT_GT(topDocs.scoreDocs[0].score, 0.0f);
    EXPECT_GT(topDocs.scoreDocs[1].score, 0.0f);
    // Higher phraseFreq should produce higher score (same IDF, shorter doc gets norm boost
    // but 3x freq should dominate)
    EXPECT_GT(topDocs.scoreDocs[0].score, topDocs.scoreDocs[1].score)
        << "Doc with 3 phrase matches should score higher than doc with 1 match";
}

/**
 * Test 15: Phrase with terms that don't exist returns 0 results.
 */
TEST_F(PhraseQueryTest, NonExistentTerms) {
    indexDocuments({
        "the quick brown fox",
    });

    int hits = searchPhrase({"nonexistent", "terms"});
    EXPECT_EQ(hits, 0) << "Phrase with nonexistent terms should return 0 results";
}
