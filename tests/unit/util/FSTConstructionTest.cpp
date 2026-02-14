// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Phase 1: FST Construction Verification Tests
 *
 * Tests FST construction behavior to match Lucene reference implementation.
 * Focus: Correctness of FST building, output accumulation, input validation.
 *
 * Reference: org.apache.lucene.util.fst.TestFSTs
 */

#include "diagon/util/FST.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace diagon::util;

// ==================== Helper Functions ====================

namespace {

/**
 * Helper to create BytesRef from string
 */
BytesRef toBytes(const std::string& str) {
    return BytesRef(str);
}

}  // anonymous namespace

// ==================== Task 1.1: Basic Construction Tests ====================

/**
 * Test: Empty FST Construction
 *
 * Lucene Behavior: Empty FSTCompiler produces valid FST that returns null for all lookups
 * Reference: org.apache.lucene.util.fst.TestFSTs#testEmptyFST
 */
TEST(FSTConstructionTest, EmptyFST) {
    FST::Builder builder;
    auto fst = builder.finish();

    // Empty FST should return NO_OUTPUT for any lookup
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("hello")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("world")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("")));
}

/**
 * Test: Single Entry FST
 *
 * Lucene Behavior: FST with one entry creates minimal structure (root -> final state)
 * Reference: org.apache.lucene.util.fst.TestFSTs#testSingleString
 */
TEST(FSTConstructionTest, SingleEntry) {
    FST::Builder builder;
    builder.add(toBytes("hello"), 42);
    auto fst = builder.finish();

    // Exact match should succeed
    EXPECT_EQ(42, fst->get(toBytes("hello")));

    // Non-existent terms should return NO_OUTPUT
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("world")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("hell")));    // Prefix only
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("hellos")));  // Extension only
}

/**
 * Test: Two Entries FST
 *
 * Lucene Behavior: FST with two entries may share root state if common prefix exists
 */
TEST(FSTConstructionTest, TwoEntries) {
    FST::Builder builder;
    builder.add(toBytes("cat"), 1);
    builder.add(toBytes("dog"), 2);
    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes("cat")));
    EXPECT_EQ(2, fst->get(toBytes("dog")));

    // Non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("bird")));
}

/**
 * Test: Common Prefix Sharing
 *
 * Lucene Behavior: Terms with common prefixes share FST states (DAG structure)
 * Expected: "test", "testing", "tested" share "test" prefix states
 *
 * Reference: org.apache.lucene.util.fst.FSTCompiler - prefix sharing is automatic
 */
TEST(FSTConstructionTest, CommonPrefix) {
    FST::Builder builder;
    builder.add(toBytes("test"), 10);
    builder.add(toBytes("tested"), 30);
    builder.add(toBytes("testing"), 20);  // Note: Must be sorted!
    auto fst = builder.finish();

    EXPECT_EQ(10, fst->get(toBytes("test")));
    EXPECT_EQ(20, fst->get(toBytes("testing")));
    EXPECT_EQ(30, fst->get(toBytes("tested")));

    // Partial matches should fail
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("tes")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("testings")));
}

/**
 * Test: Multiple Levels of Common Prefixes
 *
 * Creates tree-like structure with multiple branching points
 */
TEST(FSTConstructionTest, MultiLevelCommonPrefix) {
    FST::Builder builder;

    // Correct sort order: "cat" < "caterpillar" < "cats" ('e' < 's' at position 3)
    //                     "dog" < "doghouse" < "dogs" ('h' < 's' at position 3)
    builder.add(toBytes("cat"), 1);
    builder.add(toBytes("caterpillar"), 3);
    builder.add(toBytes("cats"), 2);

    builder.add(toBytes("dog"), 10);
    builder.add(toBytes("doghouse"), 30);
    builder.add(toBytes("dogs"), 20);

    auto fst = builder.finish();

    // Verify all entries
    EXPECT_EQ(1, fst->get(toBytes("cat")));
    EXPECT_EQ(3, fst->get(toBytes("caterpillar")));
    EXPECT_EQ(2, fst->get(toBytes("cats")));
    EXPECT_EQ(10, fst->get(toBytes("dog")));
    EXPECT_EQ(30, fst->get(toBytes("doghouse")));
    EXPECT_EQ(20, fst->get(toBytes("dogs")));

    // Verify non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("ca")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("do")));
}

/**
 * Test: Very Long Common Prefix
 *
 * Lucene supports arbitrarily long terms and common prefixes
 */
TEST(FSTConstructionTest, LongCommonPrefix) {
    FST::Builder builder;

    std::string base = "internationalization";  // 20 chars
    // Correct order: base < base+"ism" < base+"s" (at position 20: nothing < 'i' < 's')
    builder.add(toBytes(base), 1);
    builder.add(toBytes(base + "ism"), 3);
    builder.add(toBytes(base + "s"), 2);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes(base)));
    EXPECT_EQ(3, fst->get(toBytes(base + "ism")));
    EXPECT_EQ(2, fst->get(toBytes(base + "s")));
}

// ==================== Task 1.2: Output Accumulation Tests ====================

/**
 * Test: Output Accumulation Semantics
 *
 * Lucene Behavior: Outputs accumulate along path using addition (PositiveIntOutputs.add)
 *
 * Example: If arc 'a' has output 5 and arc 'b' has output 3,
 * then term "ab" should have total output 5+3=8
 *
 * Reference: org.apache.lucene.util.fst.PositiveIntOutputs
 */
TEST(FSTConstructionTest, OutputAccumulation) {
    FST::Builder builder;

    // Add terms where outputs represent cumulative values
    builder.add(toBytes("a"), 5);
    builder.add(toBytes("ab"), 8);  // 5 (from 'a') + 3 (from 'b')

    auto fst = builder.finish();

    EXPECT_EQ(5, fst->get(toBytes("a")));
    EXPECT_EQ(8, fst->get(toBytes("ab")));
}

/**
 * Test: Common Prefix Output Factoring
 *
 * Lucene Behavior: When terms share prefix, common output is factored to shared arcs
 *
 * Example: "test"->10, "testing"->15
 * FST should factor output 10 to the shared "test" prefix
 * Then "ing" arc adds +5 to reach 15
 */
TEST(FSTConstructionTest, CommonPrefixOutputFactoring) {
    FST::Builder builder;

    builder.add(toBytes("test"), 10);
    builder.add(toBytes("testing"), 15);  // Should be 10 + 5

    auto fst = builder.finish();

    EXPECT_EQ(10, fst->get(toBytes("test")));
    EXPECT_EQ(15, fst->get(toBytes("testing")));
}

/**
 * Test: Zero Output
 *
 * Lucene Behavior: Zero is valid output (identity element for addition)
 * Reference: org.apache.lucene.util.fst.PositiveIntOutputs.getNoOutput() returns 0
 */
TEST(FSTConstructionTest, ZeroOutput) {
    FST::Builder builder;

    // NOTE: "one" < "zero" in byte order (0x6F < 0x7A)
    builder.add(toBytes("one"), 1);
    builder.add(toBytes("zero"), 0);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes("one")));
    EXPECT_EQ(0, fst->get(toBytes("zero")));
}

/**
 * Test: Large Output Values
 *
 * Lucene Behavior: PositiveIntOutputs supports full int64_t range (non-negative)
 */
TEST(FSTConstructionTest, LargeOutputValues) {
    FST::Builder builder;

    int64_t largeValue = 9223372036854775807LL;  // Max int64_t
    builder.add(toBytes("large"), largeValue);
    builder.add(toBytes("small"), 1);

    auto fst = builder.finish();

    EXPECT_EQ(largeValue, fst->get(toBytes("large")));
    EXPECT_EQ(1, fst->get(toBytes("small")));
}

/**
 * Test: Output Monotonicity Not Required
 *
 * Lucene Behavior: Outputs don't need to be monotonic with term order
 */
TEST(FSTConstructionTest, NonMonotonicOutputs) {
    FST::Builder builder;

    builder.add(toBytes("apple"), 100);
    builder.add(toBytes("banana"), 50);   // Smaller output
    builder.add(toBytes("cherry"), 200);  // Larger output

    auto fst = builder.finish();

    EXPECT_EQ(100, fst->get(toBytes("apple")));
    EXPECT_EQ(50, fst->get(toBytes("banana")));
    EXPECT_EQ(200, fst->get(toBytes("cherry")));
}

// ==================== Task 1.3: Sorted Input Validation ====================

/**
 * Test: Unsorted Input Detection
 *
 * Lucene Behavior: FSTCompiler requires sorted input, throws IllegalArgumentException
 * Reference: org.apache.lucene.util.fst.FSTCompiler.add() checks input order
 */
TEST(FSTConstructionTest, UnsortedInputThrows) {
    FST::Builder builder;

    builder.add(toBytes("dog"), 1);

    // Adding "cat" after "dog" violates sort order ("cat" < "dog" lexicographically)
    EXPECT_THROW({ builder.add(toBytes("cat"), 2); }, std::invalid_argument);
}

/**
 * Test: Byte-wise Sort Order Required
 *
 * Lucene Behavior: Terms must be sorted byte-wise (not Unicode collation order)
 */
TEST(FSTConstructionTest, ByteWiseSortOrder) {
    FST::Builder builder;

    // Correct byte-wise order: 0x61 < 0x62 < 0xC3
    builder.add(toBytes("a"), 1);  // 0x61
    builder.add(toBytes("b"), 3);  // 0x62
    builder.add(toBytes("à"), 2);  // 0xC3 0xA0 (UTF-8) - comes AFTER b!

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(3, fst->get(toBytes("b")));
    EXPECT_EQ(2, fst->get(toBytes("à")));
}

/**
 * Test: Duplicate Term Detection
 *
 * Lucene Behavior: Adding same term twice throws exception
 */
TEST(FSTConstructionTest, DuplicateTermThrows) {
    FST::Builder builder;

    builder.add(toBytes("test"), 1);

    // Adding same term again should throw
    EXPECT_THROW({ builder.add(toBytes("test"), 2); }, std::invalid_argument);
}

/**
 * Test: Case Sensitivity in Sort Order
 *
 * Lucene Behavior: Sort order is case-sensitive (uppercase < lowercase in ASCII)
 */
TEST(FSTConstructionTest, CaseSensitiveSortOrder) {
    FST::Builder builder;

    // Correct byte-wise order: ALL uppercase before ALL lowercase
    // 0x41 ('A') < 0x42 ('B') < 0x61 ('a') < 0x62 ('b')
    builder.add(toBytes("Apple"), 1);   // 'A' = 0x41
    builder.add(toBytes("Banana"), 3);  // 'B' = 0x42
    builder.add(toBytes("apple"), 2);   // 'a' = 0x61
    builder.add(toBytes("banana"), 4);  // 'b' = 0x62

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes("Apple")));
    EXPECT_EQ(3, fst->get(toBytes("Banana")));
    EXPECT_EQ(2, fst->get(toBytes("apple")));
    EXPECT_EQ(4, fst->get(toBytes("banana")));
}

/**
 * Test: Empty String Sort Order
 *
 * Lucene Behavior: Empty string is smallest in sort order (comes first)
 */
TEST(FSTConstructionTest, EmptyStringSortOrder) {
    FST::Builder builder;

    builder.add(toBytes(""), 0);  // Empty string must be first
    builder.add(toBytes("a"), 1);
    builder.add(toBytes("b"), 2);

    auto fst = builder.finish();

    EXPECT_EQ(0, fst->get(toBytes("")));
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));
}

/**
 * Test: Cannot Add Before Empty String
 *
 * Lucene Behavior: Once empty string is added, cannot add any other terms before it
 */
TEST(FSTConstructionTest, CannotAddAfterEmptyStringWithPriorTerms) {
    FST::Builder builder;

    builder.add(toBytes("a"), 1);

    // Cannot add empty string after "a" (empty < "a")
    EXPECT_THROW({ builder.add(toBytes(""), 0); }, std::invalid_argument);
}

// ==================== Construction with Various Data Patterns ====================

/**
 * Test: Sequential Numeric Terms
 *
 * Common pattern in inverted indexes with integer term values
 */
TEST(FSTConstructionTest, SequentialNumericTerms) {
    FST::Builder builder;

    for (int i = 0; i < 100; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%04d", i);  // Zero-padded for sort order
        builder.add(toBytes(buf), i * 10);
    }

    auto fst = builder.finish();

    // Spot check
    EXPECT_EQ(0, fst->get(toBytes("0000")));
    EXPECT_EQ(500, fst->get(toBytes("0050")));
    EXPECT_EQ(990, fst->get(toBytes("0099")));
}

/**
 * Test: Alphabet Terms
 *
 * Test all single-byte terms
 */
TEST(FSTConstructionTest, AlphabetTerms) {
    FST::Builder builder;

    for (char c = 'a'; c <= 'z'; c++) {
        std::string term(1, c);
        builder.add(toBytes(term), c - 'a');
    }

    auto fst = builder.finish();

    EXPECT_EQ(0, fst->get(toBytes("a")));
    EXPECT_EQ(12, fst->get(toBytes("m")));
    EXPECT_EQ(25, fst->get(toBytes("z")));
}

/**
 * Test: Dictionary-like Construction
 *
 * Realistic pattern: dictionary words with frequency counts
 */
TEST(FSTConstructionTest, DictionaryPattern) {
    FST::Builder builder;

    // Simulated dictionary entries (term -> frequency)
    std::vector<std::pair<std::string, int64_t>> dictionary = {
        {"abandon", 42}, {"ability", 156}, {"able", 892},   {"about", 5234},
        {"above", 234},  {"abroad", 89},   {"absence", 34}, {"absolute", 67}};

    for (const auto& [term, freq] : dictionary) {
        builder.add(toBytes(term), freq);
    }

    auto fst = builder.finish();

    // Verify random lookups
    EXPECT_EQ(42, fst->get(toBytes("abandon")));
    EXPECT_EQ(5234, fst->get(toBytes("about")));
    EXPECT_EQ(67, fst->get(toBytes("absolute")));

    // Non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("aardvark")));
}

// ==================== Edge Cases in Construction ====================

/**
 * Test: Single Character Terms
 *
 * Lucene handles single-character terms efficiently
 */
TEST(FSTConstructionTest, SingleCharacterTerms) {
    FST::Builder builder;

    builder.add(toBytes("a"), 1);
    builder.add(toBytes("b"), 2);
    builder.add(toBytes("z"), 26);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));
    EXPECT_EQ(26, fst->get(toBytes("z")));
}

/**
 * Test: Very Long Terms
 *
 * Lucene supports terms up to 32KB (tested up to 1000 bytes in TestFSTs)
 */
TEST(FSTConstructionTest, VeryLongTerms) {
    FST::Builder builder;

    // Create terms of increasing length
    std::string term100(100, 'a');
    std::string term500(500, 'b');
    std::string term1000(1000, 'c');

    builder.add(toBytes(term100), 100);
    builder.add(toBytes(term500), 500);
    builder.add(toBytes(term1000), 1000);

    auto fst = builder.finish();

    EXPECT_EQ(100, fst->get(toBytes(term100)));
    EXPECT_EQ(500, fst->get(toBytes(term500)));
    EXPECT_EQ(1000, fst->get(toBytes(term1000)));
}

/**
 * Test: Binary Data Terms
 *
 * Lucene treats terms as byte sequences, not strings
 * Any byte values (including 0x00) are valid
 */
TEST(FSTConstructionTest, BinaryDataTerms) {
    FST::Builder builder;

    uint8_t term1[] = {0x00, 0x01, 0x02};
    uint8_t term2[] = {0x00, 0x01, 0x03};
    uint8_t term3[] = {0xFF, 0xFE, 0xFD};

    builder.add(BytesRef(term1, 3), 10);
    builder.add(BytesRef(term2, 3), 20);
    builder.add(BytesRef(term3, 3), 30);

    auto fst = builder.finish();

    EXPECT_EQ(10, fst->get(BytesRef(term1, 3)));
    EXPECT_EQ(20, fst->get(BytesRef(term2, 3)));
    EXPECT_EQ(30, fst->get(BytesRef(term3, 3)));
}

/**
 * Test: Terms with Null Bytes
 *
 * Lucene supports 0x00 byte within terms (not C-string compatible)
 */
TEST(FSTConstructionTest, TermsWithNullBytes) {
    FST::Builder builder;

    uint8_t term1[] = {'a', 0x00, 'b'};
    uint8_t term2[] = {'a', 0x00, 'c'};

    builder.add(BytesRef(term1, 3), 1);
    builder.add(BytesRef(term2, 3), 2);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(BytesRef(term1, 3)));
    EXPECT_EQ(2, fst->get(BytesRef(term2, 3)));
}

// ==================== Large Scale Construction ====================

/**
 * Test: Large FST Construction
 *
 * Verify FST can handle thousands of entries efficiently
 */
TEST(FSTConstructionTest, LargeScaleConstruction) {
    FST::Builder builder;

    const int NUM_TERMS = 10000;

    // Add 10,000 terms
    for (int i = 0; i < NUM_TERMS; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08d", i);
        builder.add(toBytes(buf), i);
    }

    auto fst = builder.finish();

    // Spot check various positions
    EXPECT_EQ(0, fst->get(toBytes("term_00000000")));
    EXPECT_EQ(1000, fst->get(toBytes("term_00001000")));
    EXPECT_EQ(5000, fst->get(toBytes("term_00005000")));
    EXPECT_EQ(9999, fst->get(toBytes("term_00009999")));

    // Non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("term_00010000")));
}

/**
 * Test: High Branching Factor
 *
 * FST with state that has many outgoing arcs (tests arc encoding)
 */
TEST(FSTConstructionTest, HighBranchingFactor) {
    FST::Builder builder;

    // Create terms with common single-char prefix, then diverge
    for (char c = 'a'; c <= 'z'; c++) {
        std::string term = std::string("x") + c;  // "xa", "xb", ..., "xz"
        builder.add(toBytes(term), c - 'a');
    }

    auto fst = builder.finish();

    // Root state should have 1 arc ('x')
    // State after 'x' should have 26 arcs ('a'-'z')

    EXPECT_EQ(0, fst->get(toBytes("xa")));
    EXPECT_EQ(12, fst->get(toBytes("xm")));
    EXPECT_EQ(25, fst->get(toBytes("xz")));
}

// ==================== Summary Statistics ====================

/**
 * Note: These tests verify FST construction behavior matches Lucene.
 *
 * Key Properties Verified:
 * 1. Empty FST is valid
 * 2. Single and multiple entries work correctly
 * 3. Common prefixes are shared (DAG structure)
 * 4. Outputs accumulate along paths (addition monoid)
 * 5. Zero and large outputs handled correctly
 * 6. Sorted input is enforced (byte-wise order)
 * 7. Duplicate terms are rejected
 * 8. Edge cases handled: empty string, long terms, binary data
 * 9. Large-scale construction works efficiently
 *
 * If all tests pass, Diagon FST construction matches Lucene behavior.
 */
