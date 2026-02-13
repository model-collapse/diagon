// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Phase 3: FST Iteration Verification Tests
 *
 * Tests FST iteration behavior to match Lucene reference implementation.
 * Focus: Correctness of iteration order, completeness, and edge cases.
 *
 * Reference: org.apache.lucene.util.fst.TestFSTs, IntsRefFSTEnum
 */

#include "diagon/util/FST.h"

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <set>

using namespace diagon::util;

// ==================== Helper Functions ====================

namespace {

/**
 * Helper to create BytesRef from string
 */
BytesRef toBytes(const std::string& str) {
    return BytesRef(str);
}

/**
 * Helper to build test FST from vector of {term, output} pairs
 * Terms must be pre-sorted
 */
std::unique_ptr<FST> buildTestFST(const std::vector<std::pair<std::string, int64_t>>& entries) {
    FST::Builder builder;
    for (const auto& [term, output] : entries) {
        builder.add(toBytes(term), output);
    }
    return builder.finish();
}

/**
 * Helper to collect all terms from FST iteration
 */
std::vector<std::pair<std::string, int64_t>> collectAllTerms(const FST& fst) {
    std::vector<std::pair<std::string, int64_t>> result;

    const auto& entries = fst.getAllEntries();
    for (const auto& [termBytes, output] : entries) {
        std::string term(reinterpret_cast<const char*>(termBytes.data()),
                        termBytes.size());
        result.emplace_back(term, output);
    }

    return result;
}

} // anonymous namespace

// ==================== Task 3.1: Iteration Order Tests ====================

/**
 * Test: Iteration Order Matches Input Order
 *
 * Lucene Behavior: Iterator returns terms in byte-wise sorted order
 * Reference: org.apache.lucene.util.fst.IntsRefFSTEnum
 */
TEST(FSTIterationTest, IterationOrderMatchesInputOrder) {
    auto fst = buildTestFST({
        {"apple", 1},
        {"banana", 2},
        {"cherry", 3}
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(3, terms.size());
    EXPECT_EQ("apple", terms[0].first);
    EXPECT_EQ(1, terms[0].second);
    EXPECT_EQ("banana", terms[1].first);
    EXPECT_EQ(2, terms[1].second);
    EXPECT_EQ("cherry", terms[2].first);
    EXPECT_EQ(3, terms[2].second);
}

/**
 * Test: Empty String Appears First
 *
 * Lucene Behavior: Empty string (if present) is smallest term
 */
TEST(FSTIterationTest, EmptyStringAppearsFirst) {
    FST::Builder builder;
    builder.add(toBytes(""), 100);
    builder.add(toBytes("a"), 1);
    builder.add(toBytes("z"), 26);
    auto fst = builder.finish();

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(3, terms.size());
    EXPECT_EQ("", terms[0].first);
    EXPECT_EQ(100, terms[0].second);
    EXPECT_EQ("a", terms[1].first);
    EXPECT_EQ("z", terms[2].first);
}

/**
 * Test: Byte-Wise Sort Order
 *
 * Lucene Behavior: Terms sorted by byte comparison (memcmp)
 */
TEST(FSTIterationTest, ByteWiseSortOrder) {
    // Byte order: 0x61 < 0x62 < 0xC3 < 0xE6
    // a < b < à < 日
    auto fst = buildTestFST({
        {"a", 1},      // 0x61
        {"b", 2},      // 0x62
        {"à", 3},      // 0xC3 0xA0
        {"日", 4}      // 0xE6 0x97 0xA5
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(4, terms.size());
    EXPECT_EQ("a", terms[0].first);
    EXPECT_EQ("b", terms[1].first);
    EXPECT_EQ("à", terms[2].first);
    EXPECT_EQ("日", terms[3].first);
}

/**
 * Test: Case-Sensitive Sort Order
 *
 * Lucene Behavior: Uppercase (0x41-0x5A) comes before lowercase (0x61-0x7A)
 */
TEST(FSTIterationTest, CaseSensitiveSortOrder) {
    auto fst = buildTestFST({
        {"Apple", 1},   // 0x41...
        {"Banana", 2},  // 0x42...
        {"apple", 3},   // 0x61...
        {"banana", 4}   // 0x62...
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(4, terms.size());
    EXPECT_EQ("Apple", terms[0].first);
    EXPECT_EQ("Banana", terms[1].first);
    EXPECT_EQ("apple", terms[2].first);
    EXPECT_EQ("banana", terms[3].first);
}

/**
 * Test: Common Prefix Ordering
 *
 * Lucene Behavior: Shorter prefix comes before longer extension
 */
TEST(FSTIterationTest, CommonPrefixOrdering) {
    auto fst = buildTestFST({
        {"cat", 1},
        {"caterpillar", 2},
        {"cats", 3},
        {"dog", 4},
        {"doghouse", 5}
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(5, terms.size());
    EXPECT_EQ("cat", terms[0].first);
    EXPECT_EQ("caterpillar", terms[1].first);
    EXPECT_EQ("cats", terms[2].first);
    EXPECT_EQ("dog", terms[3].first);
    EXPECT_EQ("doghouse", terms[4].first);
}

/**
 * Test: All Entries Returned Exactly Once
 *
 * Lucene Behavior: Iterator returns complete set, no duplicates
 */
TEST(FSTIterationTest, AllEntriesReturnedExactlyOnce) {
    // Build FST with 100 terms
    FST::Builder builder;
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%03d", i);
        builder.add(toBytes(buf), i);
    }
    auto fst = builder.finish();

    auto terms = collectAllTerms(*fst);

    // All 100 terms returned
    ASSERT_EQ(100, terms.size());

    // No duplicates
    std::set<std::string> unique_terms;
    for (const auto& [term, output] : terms) {
        EXPECT_TRUE(unique_terms.insert(term).second) << "Duplicate term: " << term;
    }

    // Check all outputs present
    std::set<int64_t> outputs;
    for (const auto& [term, output] : terms) {
        outputs.insert(output);
    }
    EXPECT_EQ(100, outputs.size());
}

// ==================== Task 3.2: Edge Case Iteration Tests ====================

/**
 * Test: Empty FST Iteration
 *
 * Lucene Behavior: Iterator over empty FST returns no terms
 */
TEST(FSTIterationTest, EmptyFSTIteration) {
    FST::Builder builder;
    auto fst = builder.finish();

    auto terms = collectAllTerms(*fst);

    EXPECT_EQ(0, terms.size());
}

/**
 * Test: Single Entry Iteration
 *
 * Lucene Behavior: Iterator over single-entry FST returns that entry
 */
TEST(FSTIterationTest, SingleEntryIteration) {
    auto fst = buildTestFST({{"hello", 42}});

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(1, terms.size());
    EXPECT_EQ("hello", terms[0].first);
    EXPECT_EQ(42, terms[0].second);
}

/**
 * Test: Large FST Iteration
 *
 * Lucene Behavior: Iterator works correctly with large FST (10K terms)
 */
TEST(FSTIterationTest, LargeFSTIteration) {
    FST::Builder builder;

    // Build FST with 10,000 terms
    for (int i = 0; i < 10000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08d", i);
        builder.add(toBytes(buf), i);
    }
    auto fst = builder.finish();

    auto terms = collectAllTerms(*fst);

    // All 10K terms returned
    ASSERT_EQ(10000, terms.size());

    // Verify order (spot checks)
    EXPECT_EQ("term_00000000", terms[0].first);
    EXPECT_EQ(0, terms[0].second);
    EXPECT_EQ("term_00005000", terms[5000].first);
    EXPECT_EQ(5000, terms[5000].second);
    EXPECT_EQ("term_00009999", terms[9999].first);
    EXPECT_EQ(9999, terms[9999].second);

    // Verify all terms are unique
    std::set<std::string> unique_terms;
    for (const auto& [term, output] : terms) {
        unique_terms.insert(term);
    }
    EXPECT_EQ(10000, unique_terms.size());
}

/**
 * Test: Single-Byte Terms Iteration
 *
 * Lucene Behavior: Single-character terms iterate in byte order
 */
TEST(FSTIterationTest, SingleByteTermsIteration) {
    auto fst = buildTestFST({
        {"a", 1},
        {"b", 2},
        {"m", 13},
        {"z", 26}
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(4, terms.size());
    EXPECT_EQ("a", terms[0].first);
    EXPECT_EQ("b", terms[1].first);
    EXPECT_EQ("m", terms[2].first);
    EXPECT_EQ("z", terms[3].first);
}

/**
 * Test: Very Long Terms Iteration
 *
 * Lucene Behavior: Very long terms (1000 bytes) iterate correctly
 */
TEST(FSTIterationTest, VeryLongTermsIteration) {
    FST::Builder builder;

    std::string term100(100, 'a');
    std::string term500(500, 'b');
    std::string term1000(1000, 'c');

    builder.add(toBytes(term100), 100);
    builder.add(toBytes(term500), 500);
    builder.add(toBytes(term1000), 1000);

    auto fst = builder.finish();
    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(3, terms.size());
    EXPECT_EQ(term100, terms[0].first);
    EXPECT_EQ(100, terms[0].second);
    EXPECT_EQ(term500, terms[1].first);
    EXPECT_EQ(500, terms[1].second);
    EXPECT_EQ(term1000, terms[2].first);
    EXPECT_EQ(1000, terms[2].second);
}

// ==================== Task 3.3: Unicode and Binary Data Tests ====================

/**
 * Test: UTF-8 Terms Iteration
 *
 * Lucene Behavior: UTF-8 terms iterate in byte-wise order
 */
TEST(FSTIterationTest, UTF8TermsIteration) {
    auto fst = buildTestFST({
        {"café", 1},        // 0x63 0x61 0x66 0xC3 0xA9
        {"naïve", 2},       // 0x6E 0x61 0xC3 0xAF 0x76 0x65
        {"résumé", 3},      // 0x72 0xC3 0xA9...
        {"日本語", 4}       // 0xE6 0x97 0xA5...
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(4, terms.size());
    EXPECT_EQ("café", terms[0].first);
    EXPECT_EQ("naïve", terms[1].first);
    EXPECT_EQ("résumé", terms[2].first);
    EXPECT_EQ("日本語", terms[3].first);
}

/**
 * Test: Binary Data Iteration
 *
 * Lucene Behavior: Binary data (non-printable bytes) iterates correctly
 */
TEST(FSTIterationTest, BinaryDataIteration) {
    FST::Builder builder;

    uint8_t data1[] = {0x00, 0x01, 0x02};
    uint8_t data2[] = {0x00, 0x01, 0x03};
    uint8_t data3[] = {0x7F, 0x80, 0xFF};

    builder.add(BytesRef(data1, 3), 1);
    builder.add(BytesRef(data2, 3), 2);
    builder.add(BytesRef(data3, 3), 3);

    auto fst = builder.finish();
    const auto& entries = fst->getAllEntries();

    ASSERT_EQ(3, entries.size());

    // First term: {0x00, 0x01, 0x02}
    EXPECT_EQ(3, entries[0].first.size());
    EXPECT_EQ(0x00, entries[0].first[0]);
    EXPECT_EQ(0x01, entries[0].first[1]);
    EXPECT_EQ(0x02, entries[0].first[2]);
    EXPECT_EQ(1, entries[0].second);

    // Second term: {0x00, 0x01, 0x03}
    EXPECT_EQ(3, entries[1].first.size());
    EXPECT_EQ(0x00, entries[1].first[0]);
    EXPECT_EQ(0x01, entries[1].first[1]);
    EXPECT_EQ(0x03, entries[1].first[2]);
    EXPECT_EQ(2, entries[1].second);

    // Third term: {0x7F, 0x80, 0xFF}
    EXPECT_EQ(3, entries[2].first.size());
    EXPECT_EQ(0x7F, entries[2].first[0]);
    EXPECT_EQ(0x80, entries[2].first[1]);
    EXPECT_EQ(0xFF, entries[2].first[2]);
    EXPECT_EQ(3, entries[2].second);
}

/**
 * Test: Terms with Null Bytes Iteration
 *
 * Lucene Behavior: Null bytes (0x00) within terms are valid
 */
TEST(FSTIterationTest, TermsWithNullBytesIteration) {
    FST::Builder builder;

    uint8_t term1[] = {'a', 0x00, 'b'};
    uint8_t term2[] = {'a', 0x00, 'c'};
    uint8_t term3[] = {'a', 'b', 'c'};  // 0x61 0x62 0x63

    builder.add(BytesRef(term1, 3), 1);
    builder.add(BytesRef(term2, 3), 2);
    builder.add(BytesRef(term3, 3), 3);

    auto fst = builder.finish();
    const auto& entries = fst->getAllEntries();

    ASSERT_EQ(3, entries.size());

    // First term: {'a', 0x00, 'b'}
    EXPECT_EQ(3, entries[0].first.size());
    EXPECT_EQ('a', entries[0].first[0]);
    EXPECT_EQ(0x00, entries[0].first[1]);
    EXPECT_EQ('b', entries[0].first[2]);

    // Second term: {'a', 0x00, 'c'}
    EXPECT_EQ(3, entries[1].first.size());
    EXPECT_EQ('a', entries[1].first[0]);
    EXPECT_EQ(0x00, entries[1].first[1]);
    EXPECT_EQ('c', entries[1].first[2]);

    // Third term: "abc"
    EXPECT_EQ(3, entries[2].first.size());
    EXPECT_EQ('a', entries[2].first[0]);
    EXPECT_EQ('b', entries[2].first[1]);
    EXPECT_EQ('c', entries[2].first[2]);
}

// ==================== Task 3.4: Iterator Behavior Tests ====================

/**
 * Test: Multiple Iterations Same FST
 *
 * Lucene Behavior: FST can be iterated multiple times independently
 */
TEST(FSTIterationTest, MultipleIterationsSameFST) {
    auto fst = buildTestFST({
        {"alpha", 1},
        {"beta", 2},
        {"gamma", 3}
    });

    // First iteration
    auto terms1 = collectAllTerms(*fst);
    ASSERT_EQ(3, terms1.size());

    // Second iteration (should return same results)
    auto terms2 = collectAllTerms(*fst);
    ASSERT_EQ(3, terms2.size());

    // Results should match
    for (size_t i = 0; i < 3; i++) {
        EXPECT_EQ(terms1[i].first, terms2[i].first);
        EXPECT_EQ(terms1[i].second, terms2[i].second);
    }
}

/**
 * Test: Repeated getAllEntries() Calls
 *
 * Lucene Behavior: Multiple calls return consistent results
 */
TEST(FSTIterationTest, RepeatedGetAllEntriesCalls) {
    auto fst = buildTestFST({
        {"one", 1},
        {"three", 3},
        {"two", 2}
    });

    // First call
    const auto& entries1 = fst->getAllEntries();
    ASSERT_EQ(3, entries1.size());

    // Second call - should return same results
    const auto& entries2 = fst->getAllEntries();
    ASSERT_EQ(3, entries2.size());

    // Results should be identical
    for (size_t i = 0; i < 3; i++) {
        EXPECT_EQ(entries1[i].first, entries2[i].first);
        EXPECT_EQ(entries1[i].second, entries2[i].second);
    }
}

/**
 * Test: Iteration After Serialization
 *
 * Lucene Behavior: FST iteration works after serialize/deserialize
 */
TEST(FSTIterationTest, IterationAfterSerialization) {
    auto original = buildTestFST({
        {"apple", 1},
        {"banana", 2},
        {"cherry", 3}
    });

    // Serialize
    std::vector<uint8_t> serialized = original->serialize();

    // Deserialize
    auto deserialized = FST::deserialize(serialized);

    // Verify iteration works
    auto terms = collectAllTerms(*deserialized);

    ASSERT_EQ(3, terms.size());
    EXPECT_EQ("apple", terms[0].first);
    EXPECT_EQ(1, terms[0].second);
    EXPECT_EQ("banana", terms[1].first);
    EXPECT_EQ(2, terms[1].second);
    EXPECT_EQ("cherry", terms[2].first);
    EXPECT_EQ(3, terms[2].second);
}

// ==================== Task 3.5: Complex Patterns Tests ====================

/**
 * Test: Nested Common Prefixes Iteration
 *
 * Lucene Behavior: Multi-level common prefixes iterate in correct order
 */
TEST(FSTIterationTest, NestedCommonPrefixesIteration) {
    auto fst = buildTestFST({
        {"pre", 1},
        {"prefix", 2},
        {"prefixes", 3},
        {"preform", 4},
        {"prepare", 5},
        {"prepared", 6}
    });

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(6, terms.size());
    EXPECT_EQ("pre", terms[0].first);
    EXPECT_EQ("prefix", terms[1].first);
    EXPECT_EQ("prefixes", terms[2].first);
    EXPECT_EQ("preform", terms[3].first);
    EXPECT_EQ("prepare", terms[4].first);
    EXPECT_EQ("prepared", terms[5].first);
}

/**
 * Test: Alphabet Iteration
 *
 * Lucene Behavior: All letters iterate in ASCII order
 */
TEST(FSTIterationTest, AlphabetIteration) {
    FST::Builder builder;
    for (char c = 'a'; c <= 'z'; c++) {
        std::string term(1, c);
        builder.add(toBytes(term), static_cast<int64_t>(c - 'a' + 1));
    }
    auto fst = builder.finish();

    auto terms = collectAllTerms(*fst);

    ASSERT_EQ(26, terms.size());
    for (int i = 0; i < 26; i++) {
        std::string expected(1, 'a' + i);
        EXPECT_EQ(expected, terms[i].first);
        EXPECT_EQ(i + 1, terms[i].second);
    }
}

/**
 * Test: Numeric String Iteration
 *
 * Lucene Behavior: Numeric strings iterate as strings (lexicographic)
 */
TEST(FSTIterationTest, NumericStringIteration) {
    auto fst = buildTestFST({
        {"1", 1},
        {"10", 10},
        {"100", 100},
        {"2", 2},
        {"20", 20}
    });

    auto terms = collectAllTerms(*fst);

    // Lexicographic order: "1" < "10" < "100" < "2" < "20"
    ASSERT_EQ(5, terms.size());
    EXPECT_EQ("1", terms[0].first);
    EXPECT_EQ("10", terms[1].first);
    EXPECT_EQ("100", terms[2].first);
    EXPECT_EQ("2", terms[3].first);
    EXPECT_EQ("20", terms[4].first);
}

// ==================== Summary Statistics ====================

/**
 * Note: These tests verify FST iteration behavior matches Lucene.
 *
 * Key Properties Verified:
 * 1. Terms returned in byte-wise sorted order
 * 2. Empty string (if present) appears first
 * 3. All entries returned exactly once
 * 4. No duplicate terms
 * 5. Outputs preserved correctly
 * 6. UTF-8 terms iterate in byte order
 * 7. Binary data (all byte values) supported
 * 8. Null bytes within terms work
 * 9. Very long terms (1000 bytes) work
 * 10. Large FST (10K terms) iterates correctly
 * 11. Multiple iterations return same results
 * 12. Iteration works after serialization
 * 13. Common prefixes iterate in correct order
 * 14. Case-sensitive ordering
 *
 * If all tests pass, Diagon FST iteration matches Lucene behavior.
 */
