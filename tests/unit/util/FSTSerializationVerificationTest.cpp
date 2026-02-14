// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Phase 5: FST Serialization Behavioral Verification Tests
 *
 * Tests FST serialization/deserialization behavior matches Lucene.
 * Focus: Correctness of roundtrip preservation, not internal format.
 *
 * Key Properties:
 * - Serialize then deserialize produces same lookup results
 * - All terms and outputs preserved
 * - Arc encoding strategies preserved
 * - getAllEntries() matches after roundtrip
 * - Multiple roundtrips produce consistent results
 *
 * Reference: org.apache.lucene.util.fst.FST (serialization format)
 */

#include "diagon/util/FST.h"

#include <gtest/gtest.h>

#include <set>
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
 * Verify two FSTs produce identical results for given terms
 */
void verifyIdenticalLookups(const FST& fst1, const FST& fst2,
                            const std::vector<std::string>& terms) {
    for (const auto& term : terms) {
        EXPECT_EQ(fst1.get(toBytes(term)), fst2.get(toBytes(term)))
            << "Mismatch for term: " << term;
    }
}

/**
 * Verify getAllEntries() matches between two FSTs
 */
void verifyIdenticalEntries(const FST& fst1, const FST& fst2) {
    const auto& entries1 = fst1.getAllEntries();
    const auto& entries2 = fst2.getAllEntries();

    ASSERT_EQ(entries1.size(), entries2.size()) << "Entry count mismatch";

    for (size_t i = 0; i < entries1.size(); i++) {
        EXPECT_EQ(entries1[i].first, entries2[i].first) << "Term mismatch at index " << i;
        EXPECT_EQ(entries1[i].second, entries2[i].second) << "Output mismatch at index " << i;
    }
}

}  // anonymous namespace

// ==================== Task 5.1: Basic Roundtrip Tests ====================

/**
 * Test: Empty FST Roundtrip
 *
 * Lucene Behavior: Empty FST serializes and deserializes correctly
 */
TEST(FSTSerializationVerificationTest, EmptyFSTRoundtrip) {
    FST::Builder builder;
    auto original = builder.finish();

    // Serialize
    auto serialized = original->serialize();

    // Deserialize
    auto deserialized = FST::deserialize(serialized);

    // Both should return NO_OUTPUT for any term
    EXPECT_EQ(FST::NO_OUTPUT, original->get(toBytes("test")));
    EXPECT_EQ(FST::NO_OUTPUT, deserialized->get(toBytes("test")));

    // getAllEntries() should match
    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Single Entry Roundtrip
 *
 * Lucene Behavior: Single term/output preserved exactly
 */
TEST(FSTSerializationVerificationTest, SingleEntryRoundtrip) {
    auto original = buildTestFST({{"hello", 42}});

    // Serialize and deserialize
    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    // Verify lookup results match
    std::vector<std::string> terms = {"hello", "hell", "hellos", "world"};
    verifyIdenticalLookups(*original, *deserialized, terms);

    // Verify getAllEntries() matches
    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Multiple Entries Roundtrip
 *
 * Lucene Behavior: All terms and outputs preserved
 */
TEST(FSTSerializationVerificationTest, MultipleEntriesRoundtrip) {
    auto original = buildTestFST(
        {{"apple", 1}, {"banana", 2}, {"cherry", 3}, {"date", 4}, {"elderberry", 5}});

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    // Verify all terms
    std::vector<std::string> terms = {"apple", "banana", "cherry", "date", "elderberry",
                                      // Non-existent
                                      "apricot", "app", "dates"};
    verifyIdenticalLookups(*original, *deserialized, terms);

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Large FST Roundtrip (10K terms)
 *
 * Lucene Behavior: Large FST preserves all data
 */
TEST(FSTSerializationVerificationTest, LargeFSTRoundtrip) {
    FST::Builder builder;
    for (int i = 0; i < 10000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08d", i);
        builder.add(toBytes(buf), i);
    }
    auto original = builder.finish();

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    // Spot check samples
    std::vector<std::string> terms;
    for (int i = 0; i < 10000; i += 1000) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%08d", i);
        terms.push_back(buf);
    }
    verifyIdenticalLookups(*original, *deserialized, terms);

    verifyIdenticalEntries(*original, *deserialized);
}

// ==================== Task 5.2: Data Type Preservation Tests ====================

/**
 * Test: Binary Data Roundtrip
 *
 * Lucene Behavior: Binary data (all byte values) preserved
 */
TEST(FSTSerializationVerificationTest, BinaryDataRoundtrip) {
    FST::Builder builder;

    uint8_t data1[] = {0x00, 0x01, 0x02, 0xFF};
    uint8_t data2[] = {0x7F, 0x80, 0xFE, 0xFF};

    builder.add(BytesRef(data1, 4), 100);
    builder.add(BytesRef(data2, 4), 200);

    auto original = builder.finish();
    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    EXPECT_EQ(100, original->get(BytesRef(data1, 4)));
    EXPECT_EQ(100, deserialized->get(BytesRef(data1, 4)));
    EXPECT_EQ(200, original->get(BytesRef(data2, 4)));
    EXPECT_EQ(200, deserialized->get(BytesRef(data2, 4)));

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: UTF-8 Data Roundtrip
 *
 * Lucene Behavior: UTF-8 sequences preserved
 */
TEST(FSTSerializationVerificationTest, UTF8DataRoundtrip) {
    auto original = buildTestFST({{"café", 1}, {"naïve", 2}, {"日本語", 3}, {"🚀", 4}});

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    std::vector<std::string> terms = {"café", "naïve", "日本語", "🚀"};
    verifyIdenticalLookups(*original, *deserialized, terms);

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Output Values Edge Cases
 *
 * Lucene Behavior: All int64_t values preserved
 */
TEST(FSTSerializationVerificationTest, OutputValuesEdgeCases) {
    // Terms must be sorted: "large" < "max" < "medium" < "one" < "small" < "zero"
    auto original = buildTestFST({{"large", 2147483647LL},
                                  {"max", 9223372036854775807LL},  // INT64_MAX
                                  {"medium", 32767},
                                  {"one", 1},
                                  {"small", 127},
                                  {"zero", 0}});

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    EXPECT_EQ(0, deserialized->get(toBytes("zero")));
    EXPECT_EQ(1, deserialized->get(toBytes("one")));
    EXPECT_EQ(127, deserialized->get(toBytes("small")));
    EXPECT_EQ(32767, deserialized->get(toBytes("medium")));
    EXPECT_EQ(2147483647LL, deserialized->get(toBytes("large")));
    EXPECT_EQ(9223372036854775807LL, deserialized->get(toBytes("max")));

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Very Long Terms Roundtrip
 *
 * Lucene Behavior: Long terms (1000+ bytes) preserved
 */
TEST(FSTSerializationVerificationTest, VeryLongTermsRoundtrip) {
    // 'a' < 'b' so 1000 'a's comes before 500 'b's
    std::string term1000(1000, 'a');
    std::string term500(500, 'b');

    auto original = buildTestFST({{term1000, 1000}, {term500, 500}});

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    EXPECT_EQ(500, deserialized->get(toBytes(term500)));
    EXPECT_EQ(1000, deserialized->get(toBytes(term1000)));

    verifyIdenticalEntries(*original, *deserialized);
}

// ==================== Task 5.3: Structure Preservation Tests ====================

/**
 * Test: All Arc Encoding Types Preserved
 *
 * Lucene Behavior: Different arc encodings work after roundtrip
 */
TEST(FSTSerializationVerificationTest, AllArcEncodingTypesPreserved) {
    FST::Builder builder;

    // LINEAR_SCAN: Few arcs
    builder.add(toBytes("a1"), 1);
    builder.add(toBytes("a2"), 2);

    // CONTINUOUS: Sequential labels
    builder.add(toBytes("b0"), 3);
    builder.add(toBytes("b1"), 4);
    builder.add(toBytes("b2"), 5);
    builder.add(toBytes("b3"), 6);
    builder.add(toBytes("b4"), 7);

    // BINARY_SEARCH: Many sparse arcs (c, e, g, i, k, m)
    builder.add(toBytes("c0"), 'c' - 'a');

    // DIRECT_ADDRESSING: Dense arcs (densed, densee, ..., densem)
    // "dense" comes before "e0", "g0", etc. (0x64 0x65 0x6E... < 0x65 0x30, etc.)
    for (char c = 'd'; c <= 'm'; c++) {
        std::string term = "dense";
        term += c;
        builder.add(toBytes(term), c - 'a' + 100);
    }

    // Continue BINARY_SEARCH terms
    builder.add(toBytes("e0"), 'e' - 'a');
    builder.add(toBytes("g0"), 'g' - 'a');
    builder.add(toBytes("i0"), 'i' - 'a');
    builder.add(toBytes("k0"), 'k' - 'a');
    builder.add(toBytes("m0"), 'm' - 'a');

    auto original = builder.finish();
    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    // Verify all lookups match
    std::vector<std::string> terms = {"a1", "a2",     "b0",     "b4", "c0",
                                      "m0", "densed", "densem", "a3", "nonexistent"};
    verifyIdenticalLookups(*original, *deserialized, terms);

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Shared Prefixes Preserved
 *
 * Lucene Behavior: FST structure with shared prefixes works after roundtrip
 */
TEST(FSTSerializationVerificationTest, SharedPrefixesPreserved) {
    auto original = buildTestFST(
        {{"cat", 1}, {"caterpillar", 2}, {"cats", 3}, {"dog", 4}, {"doghouse", 5}, {"dogs", 6}});

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    std::vector<std::string> terms = {
        "cat", "caterpillar", "cats", "dog", "doghouse", "dogs", "ca", "do"  // Partial prefixes
    };
    verifyIdenticalLookups(*original, *deserialized, terms);

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Empty String Term Preserved
 *
 * Lucene Behavior: Empty string term serializes correctly
 */
TEST(FSTSerializationVerificationTest, EmptyStringTermPreserved) {
    FST::Builder builder;
    builder.add(toBytes(""), 100);
    builder.add(toBytes("a"), 1);
    builder.add(toBytes("z"), 26);

    auto original = builder.finish();
    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    EXPECT_EQ(100, original->get(toBytes("")));
    EXPECT_EQ(100, deserialized->get(toBytes("")));
    EXPECT_EQ(1, deserialized->get(toBytes("a")));
    EXPECT_EQ(26, deserialized->get(toBytes("z")));

    verifyIdenticalEntries(*original, *deserialized);
}

// ==================== Task 5.4: Multiple Roundtrips Tests ====================

/**
 * Test: Double Roundtrip Produces Same Result
 *
 * Lucene Behavior: Serialize-deserialize-serialize-deserialize consistent
 */
TEST(FSTSerializationVerificationTest, DoubleRoundtripConsistent) {
    auto original = buildTestFST({{"apple", 1}, {"banana", 2}, {"cherry", 3}});

    // First roundtrip
    auto serialized1 = original->serialize();
    auto deserialized1 = FST::deserialize(serialized1);

    // Second roundtrip
    auto serialized2 = deserialized1->serialize();
    auto deserialized2 = FST::deserialize(serialized2);

    // All three should produce same results
    std::vector<std::string> terms = {"apple", "banana", "cherry", "date"};
    verifyIdenticalLookups(*original, *deserialized1, terms);
    verifyIdenticalLookups(*original, *deserialized2, terms);
    verifyIdenticalLookups(*deserialized1, *deserialized2, terms);

    // getAllEntries() should match
    verifyIdenticalEntries(*original, *deserialized1);
    verifyIdenticalEntries(*original, *deserialized2);
}

/**
 * Test: Triple Roundtrip Produces Same Result
 *
 * Lucene Behavior: Multiple roundtrips are idempotent
 */
TEST(FSTSerializationVerificationTest, TripleRoundtripConsistent) {
    auto original = buildTestFST({{"test", 42}});

    auto d1 = FST::deserialize(original->serialize());
    auto d2 = FST::deserialize(d1->serialize());
    auto d3 = FST::deserialize(d2->serialize());

    EXPECT_EQ(42, original->get(toBytes("test")));
    EXPECT_EQ(42, d1->get(toBytes("test")));
    EXPECT_EQ(42, d2->get(toBytes("test")));
    EXPECT_EQ(42, d3->get(toBytes("test")));

    verifyIdenticalEntries(*original, *d3);
}

/**
 * Test: Serialized Format is Stable
 *
 * Lucene Behavior: Same FST produces same serialized bytes
 */
TEST(FSTSerializationVerificationTest, SerializedFormatStable) {
    auto fst1 = buildTestFST({{"a", 1}, {"b", 2}, {"c", 3}});

    auto fst2 = buildTestFST({{"a", 1}, {"b", 2}, {"c", 3}});

    auto serialized1 = fst1->serialize();
    auto serialized2 = fst2->serialize();

    // Same input should produce same serialized output
    EXPECT_EQ(serialized1.size(), serialized2.size());
    EXPECT_EQ(serialized1, serialized2);
}

// ==================== Task 5.5: Serialization Size Tests ====================

/**
 * Test: Serialization is Compact
 *
 * Lucene Behavior: Serialized size is reasonable
 */
TEST(FSTSerializationVerificationTest, SerializationIsCompact) {
    // Small FST should have small serialized size
    auto small = buildTestFST({{"a", 1}});
    auto smallSerialized = small->serialize();
    EXPECT_LT(smallSerialized.size(), 100) << "Small FST too large";

    // Large FST should be reasonably compact
    FST::Builder builder;
    for (int i = 0; i < 1000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        builder.add(toBytes(buf), i);
    }
    auto large = builder.finish();
    auto largeSerialized = large->serialize();

    // 1000 terms, each ~10 bytes, should be reasonably compact
    // Actual size is ~30KB (without aggressive compression)
    EXPECT_LT(largeSerialized.size(), 50000) << "Large FST not compact enough";
}

/**
 * Test: Empty FST Has Minimal Size
 *
 * Lucene Behavior: Empty FST serializes to minimal bytes
 */
TEST(FSTSerializationVerificationTest, EmptyFSTMinimalSize) {
    FST::Builder builder;
    auto empty = builder.finish();
    auto serialized = empty->serialize();

    // Empty FST should be very small (just metadata)
    EXPECT_LT(serialized.size(), 50) << "Empty FST too large";
}

// ==================== Task 5.6: Edge Case Tests ====================

/**
 * Test: Single Character Terms
 *
 * Lucene Behavior: Single-byte terms serialize correctly
 */
TEST(FSTSerializationVerificationTest, SingleCharacterTerms) {
    auto original = buildTestFST({{"a", 1}, {"b", 2}, {"z", 26}});

    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    std::vector<std::string> terms = {"a", "b", "z", "c"};
    verifyIdenticalLookups(*original, *deserialized, terms);

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: All Byte Values in Terms
 *
 * Lucene Behavior: Terms with all byte values (0x00-0xFF) work
 */
TEST(FSTSerializationVerificationTest, AllByteValuesInTerms) {
    FST::Builder builder;

    // Create terms with every byte value
    for (int i = 0; i < 256; i++) {
        uint8_t byte = static_cast<uint8_t>(i);
        builder.add(BytesRef(&byte, 1), i);
    }

    auto original = builder.finish();
    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    // Verify all byte values preserved
    for (int i = 0; i < 256; i++) {
        uint8_t byte = static_cast<uint8_t>(i);
        EXPECT_EQ(i, original->get(BytesRef(&byte, 1)));
        EXPECT_EQ(i, deserialized->get(BytesRef(&byte, 1)));
    }

    verifyIdenticalEntries(*original, *deserialized);
}

/**
 * Test: Deep Nesting (Long Chains)
 *
 * Lucene Behavior: Deep FST trees serialize correctly
 */
TEST(FSTSerializationVerificationTest, DeepNesting) {
    FST::Builder builder;

    // Create progressively longer terms (deep tree)
    std::string base = "a";
    for (int i = 0; i < 100; i++) {
        builder.add(toBytes(base), i);
        base += "a";
    }

    auto original = builder.finish();
    auto serialized = original->serialize();
    auto deserialized = FST::deserialize(serialized);

    // Spot checks
    EXPECT_EQ(0, deserialized->get(toBytes("a")));
    EXPECT_EQ(10, deserialized->get(toBytes(std::string(11, 'a'))));
    EXPECT_EQ(99, deserialized->get(toBytes(std::string(100, 'a'))));

    verifyIdenticalEntries(*original, *deserialized);
}

// ==================== Summary Statistics ====================

/**
 * Note: These tests verify FST serialization/deserialization correctness.
 *
 * Key Properties Verified:
 * 1. Roundtrip preserves all terms and outputs exactly
 * 2. Lookups produce identical results after roundtrip
 * 3. getAllEntries() matches after roundtrip
 * 4. Binary data and UTF-8 preserved correctly
 * 5. All output values preserved (0, small, large, INT64_MAX)
 * 6. All arc encoding types work after roundtrip
 * 7. Shared prefixes preserved
 * 8. Multiple roundtrips are idempotent
 * 9. Serialized format is stable (same input → same output)
 * 10. Serialization is compact
 * 11. Edge cases (empty string, single char, all bytes, deep nesting) work
 *
 * If all tests pass, Diagon FST serialization matches Lucene behavior.
 */
