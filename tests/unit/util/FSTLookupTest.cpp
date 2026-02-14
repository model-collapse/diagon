// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Phase 2: FST Lookup Verification Tests
 *
 * Tests FST lookup behavior to match Lucene reference implementation.
 * Focus: Correctness of exact match, prefix behavior, edge cases.
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

/**
 * Helper to create BytesRef from raw bytes
 */
BytesRef toBytes(const std::vector<uint8_t>& bytes) {
    return BytesRef(bytes.data(), bytes.size());
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

}  // anonymous namespace

// ==================== Task 2.1: Exact Match Lookup Tests ====================

/**
 * Test: Exact Match Found
 *
 * Lucene Behavior: Exact match returns associated output
 * Reference: org.apache.lucene.util.fst.FST.get()
 */
TEST(FSTLookupTest, ExactMatchFound) {
    auto fst = buildTestFST({{"apple", 1}, {"banana", 2}, {"cherry", 3}});

    EXPECT_EQ(1, fst->get(toBytes("apple")));
    EXPECT_EQ(2, fst->get(toBytes("banana")));
    EXPECT_EQ(3, fst->get(toBytes("cherry")));
}

/**
 * Test: Exact Match Not Found
 *
 * Lucene Behavior: Non-existent term returns null/NO_OUTPUT
 */
TEST(FSTLookupTest, ExactMatchNotFound) {
    auto fst = buildTestFST({{"apple", 1}, {"cherry", 3}});

    // Not in FST
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("banana")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("durian")));
}

/**
 * Test: Prefix Is Not Match
 *
 * Lucene Behavior: Prefix of stored term does not match (unless explicitly stored)
 */
TEST(FSTLookupTest, PrefixIsNotMatch) {
    auto fst = buildTestFST({{"testing", 10}});

    // "test" is a prefix of "testing" but not stored
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("test")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("testi")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("testin")));

    // Exact match works
    EXPECT_EQ(10, fst->get(toBytes("testing")));
}

/**
 * Test: Extension Is Not Match
 *
 * Lucene Behavior: Extension of stored term does not match
 */
TEST(FSTLookupTest, ExtensionIsNotMatch) {
    auto fst = buildTestFST({{"test", 10}});

    // "testing" is an extension of "test" but not stored
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("testing")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("tests")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("tested")));

    // Exact match works
    EXPECT_EQ(10, fst->get(toBytes("test")));
}

/**
 * Test: Both Prefix and Extension Stored
 *
 * Lucene Behavior: If both prefix and extension stored, each returns its own output
 */
TEST(FSTLookupTest, PrefixAndExtensionBothStored) {
    auto fst = buildTestFST({{"test", 10}, {"testing", 20}});

    EXPECT_EQ(10, fst->get(toBytes("test")));
    EXPECT_EQ(20, fst->get(toBytes("testing")));

    // Other prefixes/extensions not stored
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("tes")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("testings")));
}

/**
 * Test: Common Prefix Lookup
 *
 * Lucene Behavior: Terms with common prefixes look up independently
 */
TEST(FSTLookupTest, CommonPrefixLookup) {
    auto fst = buildTestFST({{"cat", 1}, {"caterpillar", 2}, {"cats", 3}});

    EXPECT_EQ(1, fst->get(toBytes("cat")));
    EXPECT_EQ(2, fst->get(toBytes("caterpillar")));
    EXPECT_EQ(3, fst->get(toBytes("cats")));

    // Partial matches fail
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("ca")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("cate")));
}

/**
 * Test: Multiple Lookups Same FST
 *
 * Lucene Behavior: FST can be queried multiple times without state corruption
 */
TEST(FSTLookupTest, MultipleLookupsSameFST) {
    auto fst = buildTestFST({{"alpha", 100}, {"beta", 200}, {"gamma", 300}});

    // Multiple lookups in random order
    EXPECT_EQ(200, fst->get(toBytes("beta")));
    EXPECT_EQ(100, fst->get(toBytes("alpha")));
    EXPECT_EQ(300, fst->get(toBytes("gamma")));
    EXPECT_EQ(200, fst->get(toBytes("beta")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("delta")));
    EXPECT_EQ(100, fst->get(toBytes("alpha")));
}

// ==================== Task 2.2: Edge Case Lookup Tests ====================

/**
 * Test: Empty String Lookup
 *
 * Lucene Behavior: Empty string is valid term
 */
TEST(FSTLookupTest, EmptyStringLookup) {
    FST::Builder builder;
    builder.add(toBytes(""), 100);
    builder.add(toBytes("a"), 1);
    auto fst = builder.finish();

    // Empty string lookup
    EXPECT_EQ(100, fst->get(toBytes("")));
    EXPECT_EQ(1, fst->get(toBytes("a")));
}

/**
 * Test: Empty String Not Stored
 *
 * Lucene Behavior: If empty string not stored, lookup returns null
 */
TEST(FSTLookupTest, EmptyStringNotStored) {
    auto fst = buildTestFST({{"a", 1}, {"b", 2}});

    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("")));
}

/**
 * Test: Single Byte Term Lookup
 *
 * Lucene Behavior: Single-byte terms work correctly
 */
TEST(FSTLookupTest, SingleByteTermLookup) {
    auto fst = buildTestFST({{"a", 1}, {"b", 2}, {"z", 26}});

    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));
    EXPECT_EQ(26, fst->get(toBytes("z")));

    // Non-existent single-byte terms
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("c")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("y")));
}

/**
 * Test: Long Term Lookup
 *
 * Lucene Behavior: Very long terms supported (tested up to 1000 bytes)
 */
TEST(FSTLookupTest, LongTermLookup) {
    FST::Builder builder;

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

    // Prefixes don't match
    std::string term99(99, 'a');
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes(term99)));
}

/**
 * Test: Binary Data Lookup
 *
 * Lucene Behavior: FST treats terms as byte sequences (not strings)
 */
TEST(FSTLookupTest, BinaryDataLookup) {
    FST::Builder builder;

    uint8_t data1[] = {0x00, 0x01, 0x02};
    uint8_t data2[] = {0x00, 0x01, 0x03};
    uint8_t data3[] = {0xFF, 0xFE, 0xFD};

    builder.add(BytesRef(data1, 3), 10);
    builder.add(BytesRef(data2, 3), 20);
    builder.add(BytesRef(data3, 3), 30);

    auto fst = builder.finish();

    EXPECT_EQ(10, fst->get(BytesRef(data1, 3)));
    EXPECT_EQ(20, fst->get(BytesRef(data2, 3)));
    EXPECT_EQ(30, fst->get(BytesRef(data3, 3)));

    // Non-existent binary data
    uint8_t data4[] = {0x00, 0x02, 0x00};
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef(data4, 3)));
}

/**
 * Test: Null Byte Within Term
 *
 * Lucene Behavior: Null byte (0x00) is valid within term (not C-string terminator)
 */
TEST(FSTLookupTest, NullByteWithinTerm) {
    FST::Builder builder;

    uint8_t term1[] = {'a', 0x00, 'b'};
    uint8_t term2[] = {'a', 0x00, 'c'};

    builder.add(BytesRef(term1, 3), 1);
    builder.add(BytesRef(term2, 3), 2);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(BytesRef(term1, 3)));
    EXPECT_EQ(2, fst->get(BytesRef(term2, 3)));

    // Partial match with null byte doesn't work
    uint8_t term3[] = {'a', 0x00};
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef(term3, 2)));
}

/**
 * Test: All Byte Values
 *
 * Lucene Behavior: All byte values 0x00-0xFF are valid in terms
 */
TEST(FSTLookupTest, AllByteValues) {
    FST::Builder builder;

    // Create terms with each byte value
    for (int i = 0; i < 256; i++) {
        uint8_t byte = static_cast<uint8_t>(i);
        builder.add(BytesRef(&byte, 1), i);
    }

    auto fst = builder.finish();

    // Verify all byte values can be looked up
    for (int i = 0; i < 256; i++) {
        uint8_t byte = static_cast<uint8_t>(i);
        EXPECT_EQ(i, fst->get(BytesRef(&byte, 1)));
    }
}

// ==================== Task 2.3: Unicode and UTF-8 Tests ====================

/**
 * Test: Multi-Byte UTF-8 Lookup
 *
 * Lucene Behavior: FST treats UTF-8 as byte sequences (byte-wise comparison)
 */
TEST(FSTLookupTest, MultiByteUTF8Lookup) {
    auto fst = buildTestFST({
        {"café", 1},    // é = 2 bytes (0xC3 0xA9)
        {"日本語", 2},  // 3 bytes per character
        {"🚀", 3}        // 4-byte emoji
    });

    EXPECT_EQ(1, fst->get(toBytes("café")));
    EXPECT_EQ(2, fst->get(toBytes("日本語")));
    EXPECT_EQ(3, fst->get(toBytes("🚀")));
}

/**
 * Test: UTF-8 Partial Match
 *
 * Lucene Behavior: Partial UTF-8 sequence doesn't match (byte boundaries)
 */
TEST(FSTLookupTest, UTF8PartialMatch) {
    auto fst = buildTestFST({{"café", 1}});

    // Full match works
    EXPECT_EQ(1, fst->get(toBytes("café")));

    // Partial ASCII prefix doesn't match
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("caf")));

    // Including partial UTF-8 sequence
    std::vector<uint8_t> partial = {'c', 'a', 'f', 0xC3};  // Missing 0xA9
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes(partial)));
}

/**
 * Test: UTF-8 Sort Order
 *
 * Lucene Behavior: Terms sorted byte-wise (UTF-8 byte order, not Unicode collation)
 */
TEST(FSTLookupTest, UTF8SortOrder) {
    FST::Builder builder;

    // Byte-wise order: 0x61 < 0x62 < 0xC3
    builder.add(toBytes("a"), 1);  // 0x61
    builder.add(toBytes("b"), 2);  // 0x62
    builder.add(toBytes("à"), 3);  // 0xC3 0xA0

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));
    EXPECT_EQ(3, fst->get(toBytes("à")));
}

/**
 * Test: Combining Characters
 *
 * Lucene Behavior: Precomposed vs decomposed treated as different byte sequences
 */
TEST(FSTLookupTest, CombiningCharacters) {
    FST::Builder builder;

    // é can be single code point (U+00E9) or combining (e + U+0301)
    std::string precomposed = "café";       // é = 0xC3 0xA9
    std::string decomposed = "cafe\u0301";  // e + combining accent

    // These are different byte sequences
    builder.add(toBytes(decomposed), 1);
    builder.add(toBytes(precomposed), 2);

    auto fst = builder.finish();

    // Each matches its own form
    EXPECT_EQ(1, fst->get(toBytes(decomposed)));
    EXPECT_EQ(2, fst->get(toBytes(precomposed)));

    // They don't cross-match (different bytes)
    EXPECT_NE(precomposed, decomposed);
}

/**
 * Test: Mixed ASCII and UTF-8
 *
 * Lucene Behavior: ASCII and UTF-8 can be mixed in same FST
 */
TEST(FSTLookupTest, MixedASCIIAndUTF8) {
    // Correct byte-wise order: 0x61 < 0x63 < 0x7A < 0xE6
    // "apple" (0x61...) < "café" (0x63...) < "zebra" (0x7A...) < "日本語" (0xE6...)
    auto fst = buildTestFST({{"apple", 1}, {"café", 2}, {"zebra", 4}, {"日本語", 3}});

    EXPECT_EQ(1, fst->get(toBytes("apple")));
    EXPECT_EQ(2, fst->get(toBytes("café")));
    EXPECT_EQ(4, fst->get(toBytes("zebra")));
    EXPECT_EQ(3, fst->get(toBytes("日本語")));
}

// ==================== Lookup Performance Tests ====================

/**
 * Test: Large FST Lookup Performance
 *
 * Verify lookup works correctly with large FST
 */
TEST(FSTLookupTest, LargeFSTLookup) {
    FST::Builder builder;

    // Build FST with 10,000 terms
    for (int i = 0; i < 10000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08d", i);
        builder.add(toBytes(buf), i);
    }

    auto fst = builder.finish();

    // Spot check lookups
    EXPECT_EQ(0, fst->get(toBytes("term_00000000")));
    EXPECT_EQ(1000, fst->get(toBytes("term_00001000")));
    EXPECT_EQ(5000, fst->get(toBytes("term_00005000")));
    EXPECT_EQ(9999, fst->get(toBytes("term_00009999")));

    // Non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("term_00010000")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("not_a_term")));
}

/**
 * Test: Lookup After Serialization
 *
 * Lucene Behavior: FST can be serialized/deserialized and lookups still work
 */
TEST(FSTLookupTest, LookupAfterSerialization) {
    auto original = buildTestFST({{"apple", 1}, {"banana", 2}, {"cherry", 3}});

    // Serialize
    std::vector<uint8_t> serialized = original->serialize();

    // Deserialize
    auto deserialized = FST::deserialize(serialized);

    // Verify all lookups work
    EXPECT_EQ(1, deserialized->get(toBytes("apple")));
    EXPECT_EQ(2, deserialized->get(toBytes("banana")));
    EXPECT_EQ(3, deserialized->get(toBytes("cherry")));

    // Non-existent
    EXPECT_EQ(FST::NO_OUTPUT, deserialized->get(toBytes("durian")));
}

// ==================== Edge Case Combinations ====================

/**
 * Test: Empty FST Lookup
 *
 * Lucene Behavior: Empty FST returns null for all lookups
 */
TEST(FSTLookupTest, EmptyFSTLookup) {
    FST::Builder builder;
    auto fst = builder.finish();

    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("anything")));
}

/**
 * Test: Single Entry FST Various Lookups
 *
 * Lucene Behavior: Single-entry FST only matches exact term
 */
TEST(FSTLookupTest, SingleEntryVariousLookups) {
    auto fst = buildTestFST({{"hello", 42}});

    // Exact match
    EXPECT_EQ(42, fst->get(toBytes("hello")));

    // Prefix
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("hell")));

    // Extension
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("hellos")));

    // Different term
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("world")));

    // Empty
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("")));
}

/**
 * Test: Case Sensitivity in Lookup
 *
 * Lucene Behavior: Lookups are case-sensitive (byte-wise)
 */
TEST(FSTLookupTest, CaseSensitivity) {
    auto fst = buildTestFST({{"Apple", 1}, {"apple", 2}});

    // Case matters
    EXPECT_EQ(1, fst->get(toBytes("Apple")));
    EXPECT_EQ(2, fst->get(toBytes("apple")));

    // Wrong case doesn't match
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("APPLE")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("aPPLE")));
}

// ==================== Summary Statistics ====================

/**
 * Note: These tests verify FST lookup behavior matches Lucene.
 *
 * Key Properties Verified:
 * 1. Exact match returns correct output
 * 2. Prefix of term doesn't match (unless explicitly stored)
 * 3. Extension of term doesn't match
 * 4. Empty string is valid term
 * 5. Single-byte terms work
 * 6. Very long terms work (1000+ bytes)
 * 7. Binary data works (all byte values 0x00-0xFF)
 * 8. Null bytes within terms work
 * 9. UTF-8 multi-byte sequences work
 * 10. Case-sensitive (byte-wise comparison)
 * 11. FST can be queried multiple times
 * 12. Lookup after serialization/deserialization works
 * 13. Large FST (10K terms) lookups work
 *
 * If all tests pass, Diagon FST lookup matches Lucene behavior.
 */
