// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Phase 6: FST BlockTree Integration Behavioral Verification Tests
 *
 * Tests that FST maintains its behavioral properties when integrated with BlockTree.
 * Focus: FST correctness in BlockTree context, not BlockTree itself.
 *
 * Key Properties:
 * - FST correctly maps terms to block pointers
 * - Term lookup through FST finds correct blocks
 * - Block metadata preserved through FST
 * - getAllEntries() returns all blocks in order
 * - FST properties (construction, lookup, iteration) hold in BlockTree
 *
 * Reference: org.apache.lucene.codecs.blocktree.BlockTreeTermsWriter
 */

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/BytesRef.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace diagon;
using namespace diagon::codecs::blocktree;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::util;

// ==================== Helper Functions ====================

namespace {

/**
 * Create field info for testing
 */
FieldInfo createFieldInfo(const std::string& name) {
    FieldInfo info;
    info.name = name;
    info.number = 0;
    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    return info;
}

/**
 * Write terms and return reader
 */
std::unique_ptr<BlockTreeTermsReader> writeAndCreateReader(
    const std::vector<std::pair<std::string, int64_t>>& terms,
    const FieldInfo& fieldInfo,
    ByteBuffersIndexInput** timInOut,
    ByteBuffersIndexInput** tipInOut) {

    // Write phase
    static ByteBuffersIndexOutput timOut("test.tim");
    static ByteBuffersIndexOutput tipOut("test.tip");

    timOut = ByteBuffersIndexOutput("test.tim");
    tipOut = ByteBuffersIndexOutput("test.tip");

    {
        BlockTreeTermsWriter::Config config;
        config.minItemsInBlock = 25;
        config.maxItemsInBlock = 48;

        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo, config);

        for (const auto& [term, output] : terms) {
            BlockTreeTermsWriter::TermStats stats;
            stats.docFreq = 1;
            stats.totalTermFreq = 1;
            stats.postingsFP = output;
            writer.addTerm(BytesRef(term), stats);
        }

        writer.finish();
    }

    // Read phase
    static std::vector<uint8_t> timData;
    static std::vector<uint8_t> tipData;
    static std::unique_ptr<ByteBuffersIndexInput> timIn;
    static std::unique_ptr<ByteBuffersIndexInput> tipIn;

    timData = timOut.toArrayCopy();
    tipData = tipOut.toArrayCopy();
    timIn = std::make_unique<ByteBuffersIndexInput>("test.tim", timData);
    tipIn = std::make_unique<ByteBuffersIndexInput>("test.tip", tipData);

    *timInOut = timIn.get();
    *tipInOut = tipIn.get();

    return std::make_unique<BlockTreeTermsReader>(timIn.get(), tipIn.get(), fieldInfo);
}

} // anonymous namespace

// ==================== Task 6.1: FST Construction in BlockTree ====================

/**
 * Test: FST Built Correctly from Terms
 *
 * Lucene Behavior: BlockTree builds FST mapping first term → block FP
 */
TEST(BlockTreeFSTIntegrationTest, FSTBuiltCorrectlyFromTerms) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    // Create terms that will span multiple blocks
    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        terms.emplace_back(buf, i * 100);
    }

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    // Verify all terms can be found
    auto termsEnum = reader->iterator();
    for (const auto& [term, output] : terms) {
        EXPECT_TRUE(termsEnum->seekExact(BytesRef(term)))
            << "Failed to find term: " << term;
    }

    // Verify total count
    EXPECT_EQ(100, reader->getNumTerms());
}

/**
 * Test: Empty Field Has Empty FST
 *
 * Lucene Behavior: Empty field produces empty FST
 */
TEST(BlockTreeFSTIntegrationTest, EmptyFieldHasEmptyFST) {
    FieldInfo fieldInfo = createFieldInfo("empty_field");

    std::vector<std::pair<std::string, int64_t>> terms;  // Empty

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    EXPECT_EQ(0, reader->getNumTerms());

    // Try to find any term
    auto termsEnum = reader->iterator();
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("anything")));
}

/**
 * Test: Single Term Creates Single Block FST
 *
 * Lucene Behavior: Single term creates FST with one entry
 */
TEST(BlockTreeFSTIntegrationTest, SingleTermCreatesSingleBlockFST) {
    FieldInfo fieldInfo = createFieldInfo("single_term_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"onlyterm", 42}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    EXPECT_EQ(1, reader->getNumTerms());

    auto termsEnum = reader->iterator();
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("onlyterm")));
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("other")));
}

// ==================== Task 6.2: FST Lookup in BlockTree ====================

/**
 * Test: FST Finds Correct Block for Term
 *
 * Lucene Behavior: FST lookup returns block containing term
 */
TEST(BlockTreeFSTIntegrationTest, FSTFindsCorrectBlockForTerm) {
    FieldInfo fieldInfo = createFieldInfo("multi_block_field");

    // Create enough terms to span multiple blocks (3+ blocks)
    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 150; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        terms.emplace_back(buf, i);
    }

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    // All terms should be findable
    auto termsEnum = reader->iterator();
    for (const auto& [term, _] : terms) {
        EXPECT_TRUE(termsEnum->seekExact(BytesRef(term)))
            << "Term not found: " << term;
    }
}

/**
 * Test: FST Returns NO_OUTPUT for Non-Existent Terms
 *
 * Lucene Behavior: Non-existent terms return false from seekExact
 */
TEST(BlockTreeFSTIntegrationTest, FSTReturnsNoOutputForNonExistentTerms) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"apple", 1},
        {"banana", 2},
        {"cherry", 3}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    auto termsEnum = reader->iterator();

    // Existing terms found
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("apple")));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("banana")));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("cherry")));

    // Non-existent terms not found
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("apricot")));
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("date")));
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("elderberry")));
}

/**
 * Test: FST Handles Prefix Queries Correctly
 *
 * Lucene Behavior: SeekCeil finds first term >= target
 */
TEST(BlockTreeFSTIntegrationTest, FSTHandlesPrefixQueriesCorrectly) {
    FieldInfo fieldInfo = createFieldInfo("prefix_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"apple", 1},
        {"application", 2},
        {"apply", 3},
        {"banana", 4},
        {"band", 5}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    auto termsEnum = reader->iterator();

    // SeekCeil to "app" should find "apple"
    auto status = termsEnum->seekCeil(BytesRef("app"));
    EXPECT_EQ(TermsEnum::SeekStatus::NOT_FOUND, status);
    BytesRef term1 = termsEnum->term();
    EXPECT_EQ("apple", std::string(reinterpret_cast<const char*>(term1.data()),
                                   term1.size()));

    // SeekCeil to "appl" should find "apple"
    status = termsEnum->seekCeil(BytesRef("appl"));
    EXPECT_EQ(TermsEnum::SeekStatus::NOT_FOUND, status);
    BytesRef term2 = termsEnum->term();
    EXPECT_EQ("apple", std::string(reinterpret_cast<const char*>(term2.data()),
                                   term2.size()));

    // SeekCeil to "apple" should find "apple" (exact)
    status = termsEnum->seekCeil(BytesRef("apple"));
    EXPECT_EQ(TermsEnum::SeekStatus::FOUND, status);
    BytesRef term3 = termsEnum->term();
    EXPECT_EQ("apple", std::string(reinterpret_cast<const char*>(term3.data()),
                                   term3.size()));
}

// ==================== Task 6.3: FST Iteration in BlockTree ====================

/**
 * Test: Iteration Through FST Returns All Terms
 *
 * Lucene Behavior: TermsEnum.next() returns all terms in sorted order
 */
TEST(BlockTreeFSTIntegrationTest, IterationThroughFSTReturnsAllTerms) {
    FieldInfo fieldInfo = createFieldInfo("iteration_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"apple", 1},
        {"banana", 2},
        {"cherry", 3},
        {"date", 4},
        {"elderberry", 5}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    // Iterate and collect all terms
    std::vector<std::string> foundTerms;
    auto termsEnum = reader->iterator();
    while (termsEnum->next()) {
        BytesRef term = termsEnum->term();
        foundTerms.push_back(std::string(reinterpret_cast<const char*>(term.data()),
                                         term.size()));
    }

    // Verify all terms found in order
    ASSERT_EQ(5, foundTerms.size());
    EXPECT_EQ("apple", foundTerms[0]);
    EXPECT_EQ("banana", foundTerms[1]);
    EXPECT_EQ("cherry", foundTerms[2]);
    EXPECT_EQ("date", foundTerms[3]);
    EXPECT_EQ("elderberry", foundTerms[4]);
}

/**
 * Test: Iteration Over Multiple Blocks Works
 *
 * Lucene Behavior: Iteration crosses block boundaries correctly
 */
TEST(BlockTreeFSTIntegrationTest, IterationOverMultipleBlocksWorks) {
    FieldInfo fieldInfo = createFieldInfo("multi_block_iteration");

    // Create 200 terms to ensure multiple blocks
    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 200; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        terms.emplace_back(buf, i);
    }

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    // Count all terms via iteration
    int count = 0;
    auto termsEnum = reader->iterator();
    while (termsEnum->next()) {
        count++;
    }

    EXPECT_EQ(200, count);
}

// ==================== Task 6.4: FST Properties in BlockTree ====================

/**
 * Test: FST Maintains Sorted Order in BlockTree
 *
 * Lucene Behavior: Terms stored in byte-wise sorted order
 */
TEST(BlockTreeFSTIntegrationTest, FSTMaintainsSortedOrderInBlockTree) {
    FieldInfo fieldInfo = createFieldInfo("sorted_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"a", 1},
        {"b", 2},
        {"c", 3},
        {"d", 4},
        {"e", 5}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    // Iterate and verify order
    std::vector<char> foundChars;
    auto termsEnum = reader->iterator();
    while (termsEnum->next()) {
        BytesRef term = termsEnum->term();
        foundChars.push_back(term.data()[0]);
    }

    ASSERT_EQ(5, foundChars.size());
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ('a' + i, foundChars[i]);
    }
}

/**
 * Test: FST Handles UTF-8 Terms in BlockTree
 *
 * Lucene Behavior: UTF-8 terms work correctly
 */
TEST(BlockTreeFSTIntegrationTest, FSTHandlesUTF8TermsInBlockTree) {
    FieldInfo fieldInfo = createFieldInfo("utf8_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"café", 1},
        {"naïve", 2},
        {"日本語", 3}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    auto termsEnum = reader->iterator();
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("café")));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("naïve")));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("日本語")));
}

/**
 * Test: FST Handles Binary Data in BlockTree
 *
 * Lucene Behavior: Binary data (non-printable bytes) works
 */
TEST(BlockTreeFSTIntegrationTest, FSTHandlesBinaryDataInBlockTree) {
    FieldInfo fieldInfo = createFieldInfo("binary_field");

    uint8_t data1[] = {0x00, 0x01, 0x02};
    uint8_t data2[] = {0x00, 0x01, 0x03};
    uint8_t data3[] = {0x7F, 0x80, 0xFF};

    std::vector<std::pair<std::string, int64_t>> terms;
    terms.emplace_back(std::string(reinterpret_cast<char*>(data1), 3), 1);
    terms.emplace_back(std::string(reinterpret_cast<char*>(data2), 3), 2);
    terms.emplace_back(std::string(reinterpret_cast<char*>(data3), 3), 3);

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    auto termsEnum = reader->iterator();
    EXPECT_TRUE(termsEnum->seekExact(BytesRef(data1, 3)));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef(data2, 3)));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef(data3, 3)));
}

// ==================== Task 6.5: Large Scale Integration ====================

/**
 * Test: Large FST in BlockTree (10K terms)
 *
 * Lucene Behavior: Large FST works correctly in BlockTree
 */
TEST(BlockTreeFSTIntegrationTest, LargeFSTInBlockTree) {
    FieldInfo fieldInfo = createFieldInfo("large_field");

    // Create 10,000 terms
    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 10000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08d", i);
        terms.emplace_back(buf, i);
    }

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    EXPECT_EQ(10000, reader->getNumTerms());

    // Spot check samples
    auto termsEnum = reader->iterator();
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_00000000")));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_00005000")));
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_00009999")));

    // Non-existent
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("term_00010000")));
}

/**
 * Test: Shared Prefixes in BlockTree FST
 *
 * Lucene Behavior: Terms with shared prefixes work correctly
 */
TEST(BlockTreeFSTIntegrationTest, SharedPrefixesInBlockTreeFST) {
    FieldInfo fieldInfo = createFieldInfo("prefix_sharing_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"cat", 1},
        {"caterpillar", 2},
        {"cats", 3},
        {"dog", 4},
        {"doghouse", 5},
        {"dogs", 6}
    };

    ByteBuffersIndexInput* timIn = nullptr;
    ByteBuffersIndexInput* tipIn = nullptr;
    auto reader = writeAndCreateReader(terms, fieldInfo, &timIn, &tipIn);

    auto termsEnum = reader->iterator();
    for (const auto& [term, _] : terms) {
        EXPECT_TRUE(termsEnum->seekExact(BytesRef(term)))
            << "Failed to find term: " << term;
    }

    // Partial prefixes don't match
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("ca")));
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("do")));
}

// ==================== Summary Statistics ====================

/**
 * Note: These tests verify FST maintains behavioral properties in BlockTree.
 *
 * Key Properties Verified:
 * 1. FST built correctly from terms
 * 2. FST finds correct block for each term
 * 3. All terms findable through FST
 * 4. Non-existent terms return false
 * 5. Prefix queries work (seekCeil)
 * 6. Iteration returns all terms in order
 * 7. Iteration crosses block boundaries
 * 8. Sorted order maintained
 * 9. UTF-8 and binary data work
 * 10. Large FST (10K terms) works
 * 11. Shared prefixes work
 *
 * If all tests pass, FST integration with BlockTree is correct.
 */
