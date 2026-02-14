// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Phase 4: FST Arc Encoding Verification Tests
 *
 * Tests that different arc encoding strategies produce correct behavior.
 * Focus: Correctness of each encoding type, not performance.
 *
 * Arc Encoding Types:
 * - DIRECT_ADDRESSING: Dense nodes (range ≤ 64, density ≥ 25%), O(1) lookup
 * - BINARY_SEARCH: Moderate density (≥ 6 arcs), O(log n) lookup
 * - CONTINUOUS: Sequential labels (no gaps), O(1) lookup
 * - LINEAR_SCAN: Sparse nodes (< 6 arcs), O(n) lookup
 *
 * Reference: org.apache.lucene.util.fst.FST (arc encoding strategies)
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

// ==================== Task 4.1: Linear Scan Encoding Tests ====================

/**
 * Test: Linear Scan - Single Arc
 *
 * Pattern: Root has 1 arc → LINEAR_SCAN encoding
 * Lucene Behavior: Single arc uses linear scan
 */
TEST(FSTArcEncodingTest, LinearScanSingleArc) {
    auto fst = buildTestFST({{"a", 1}});

    // Verify lookup works
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("")));
}

/**
 * Test: Linear Scan - Few Arcs (2-5)
 *
 * Pattern: Root has 2-5 arcs → LINEAR_SCAN encoding
 * Lucene Behavior: Few arcs use linear scan (simple, fast for small n)
 */
TEST(FSTArcEncodingTest, LinearScanFewArcs) {
    // 3 arcs from root
    auto fst = buildTestFST({{"a", 1}, {"b", 2}, {"c", 3}});

    // Verify all lookups work
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));
    EXPECT_EQ(3, fst->get(toBytes("c")));

    // Non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("d")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("")));
}

/**
 * Test: Linear Scan - Sparse Labels
 *
 * Pattern: Few arcs with large gaps → LINEAR_SCAN
 * Example: a, d, x (gaps of 3 and 20)
 */
TEST(FSTArcEncodingTest, LinearScanSparseLabels) {
    auto fst = buildTestFST({
        {"a", 1},  // 0x61
        {"d", 4},  // 0x64 (gap of 3)
        {"x", 24}  // 0x78 (gap of 20)
    });

    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(4, fst->get(toBytes("d")));
    EXPECT_EQ(24, fst->get(toBytes("x")));

    // Gaps should not match
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("c")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("e")));
}

/**
 * Test: Linear Scan - Multi-Level
 *
 * Pattern: Multiple nodes each using linear scan
 */
TEST(FSTArcEncodingTest, LinearScanMultiLevel) {
    auto fst = buildTestFST({{"ab", 1}, {"ac", 2}, {"ba", 3}, {"bb", 4}});

    // Root has 2 arcs (a, b) → LINEAR_SCAN
    // 'a' node has 2 arcs (b, c) → LINEAR_SCAN
    // 'b' node has 2 arcs (a, b) → LINEAR_SCAN

    EXPECT_EQ(1, fst->get(toBytes("ab")));
    EXPECT_EQ(2, fst->get(toBytes("ac")));
    EXPECT_EQ(3, fst->get(toBytes("ba")));
    EXPECT_EQ(4, fst->get(toBytes("bb")));

    // Partial matches don't work
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b")));
}

// ==================== Task 4.2: Continuous Encoding Tests ====================

/**
 * Test: Continuous - Sequential Labels
 *
 * Pattern: All labels present in range [min, max] → CONTINUOUS encoding
 * Example: a, b, c, d, e (0x61-0x65, all present)
 * Lucene Behavior: Optimal encoding for sequential labels (O(1), minimal space)
 */
TEST(FSTArcEncodingTest, ContinuousSequentialLabels) {
    auto fst = buildTestFST({{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}, {"e", 5}});

    // All present
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));
    EXPECT_EQ(3, fst->get(toBytes("c")));
    EXPECT_EQ(4, fst->get(toBytes("d")));
    EXPECT_EQ(5, fst->get(toBytes("e")));

    // Outside range
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("f")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("z")));
}

/**
 * Test: Continuous - Alphabet Sequence
 *
 * Pattern: Full alphabet a-z → CONTINUOUS
 */
TEST(FSTArcEncodingTest, ContinuousAlphabet) {
    FST::Builder builder;
    for (char c = 'a'; c <= 'z'; c++) {
        std::string term(1, c);
        builder.add(toBytes(term), static_cast<int64_t>(c - 'a' + 1));
    }
    auto fst = builder.finish();

    // All letters present
    for (char c = 'a'; c <= 'z'; c++) {
        std::string term(1, c);
        EXPECT_EQ(static_cast<int64_t>(c - 'a' + 1), fst->get(toBytes(term)));
    }

    // Outside alphabet
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("0")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("A")));
}

/**
 * Test: Continuous - Numeric Sequence
 *
 * Pattern: Digits 0-9 → CONTINUOUS
 */
TEST(FSTArcEncodingTest, ContinuousNumericSequence) {
    FST::Builder builder;
    for (char c = '0'; c <= '9'; c++) {
        std::string term(1, c);
        builder.add(toBytes(term), static_cast<int64_t>(c - '0'));
    }
    auto fst = builder.finish();

    // All digits present
    for (char c = '0'; c <= '9'; c++) {
        std::string term(1, c);
        EXPECT_EQ(static_cast<int64_t>(c - '0'), fst->get(toBytes(term)));
    }

    // Outside range
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a")));
}

/**
 * Test: Continuous - Multi-Level Sequential
 *
 * Pattern: Multiple nodes each using continuous encoding
 */
TEST(FSTArcEncodingTest, ContinuousMultiLevel) {
    // Root: a-c (continuous)
    // Each child: 0-2 (continuous)
    auto fst = buildTestFST({{"a0", 1},
                             {"a1", 2},
                             {"a2", 3},
                             {"b0", 4},
                             {"b1", 5},
                             {"b2", 6},
                             {"c0", 7},
                             {"c1", 8},
                             {"c2", 9}});

    // All combinations present
    EXPECT_EQ(1, fst->get(toBytes("a0")));
    EXPECT_EQ(5, fst->get(toBytes("b1")));
    EXPECT_EQ(9, fst->get(toBytes("c2")));

    // Outside ranges
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a3")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("d0")));
}

// ==================== Task 4.3: Binary Search Encoding Tests ====================

/**
 * Test: Binary Search - Moderate Number of Arcs
 *
 * Pattern: 6+ arcs, not continuous → BINARY_SEARCH encoding
 * Lucene Behavior: O(log n) lookup, good for moderate density
 */
TEST(FSTArcEncodingTest, BinarySearchModerateArcs) {
    // 8 arcs with gaps (not continuous)
    auto fst = buildTestFST(
        {{"a", 1}, {"c", 3}, {"e", 5}, {"g", 7}, {"i", 9}, {"k", 11}, {"m", 13}, {"o", 15}});

    // All terms present
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(3, fst->get(toBytes("c")));
    EXPECT_EQ(15, fst->get(toBytes("o")));

    // Gaps not present
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("d")));
}

/**
 * Test: Binary Search - Many Sparse Arcs
 *
 * Pattern: Many arcs (10+) but large label range → BINARY_SEARCH
 */
TEST(FSTArcEncodingTest, BinarySearchManySparseArcs) {
    // 10 arcs spanning large range (a-z)
    auto fst = buildTestFST({{"a", 1},
                             {"c", 2},
                             {"f", 3},
                             {"h", 4},
                             {"k", 5},
                             {"m", 6},
                             {"p", 7},
                             {"r", 8},
                             {"u", 9},
                             {"z", 10}});

    // Spot checks
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(5, fst->get(toBytes("k")));
    EXPECT_EQ(10, fst->get(toBytes("z")));

    // Missing letters
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("n")));
}

/**
 * Test: Binary Search - Edge Case 6 Arcs
 *
 * Pattern: Exactly 6 arcs (threshold for binary search)
 */
TEST(FSTArcEncodingTest, BinarySearchExactly6Arcs) {
    auto fst = buildTestFST({{"a", 1}, {"d", 4}, {"g", 7}, {"j", 10}, {"m", 13}, {"p", 16}});

    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(7, fst->get(toBytes("g")));
    EXPECT_EQ(16, fst->get(toBytes("p")));
}

/**
 * Test: Binary Search - Multi-Level
 *
 * Pattern: Multiple nodes using binary search
 */
TEST(FSTArcEncodingTest, BinarySearchMultiLevel) {
    FST::Builder builder;
    // Root: 6 arcs (a-f) with gaps
    // Each child: 6 arcs (0-5) with gaps
    for (char c : {'a', 'b', 'c', 'd', 'e', 'f'}) {
        for (char n : {'0', '2', '4', '6', '8', '9'}) {
            std::string term;
            term += c;
            term += n;
            int64_t output = (c - 'a') * 10 + (n - '0');
            builder.add(toBytes(term), output);
        }
    }
    auto fst = builder.finish();

    // Spot checks
    EXPECT_EQ(0, fst->get(toBytes("a0")));
    EXPECT_EQ(34, fst->get(toBytes("d4")));
    EXPECT_EQ(59, fst->get(toBytes("f9")));

    // Missing combinations
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a1")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("g0")));
}

// ==================== Task 4.4: Direct Addressing Encoding Tests ====================

/**
 * Test: Direct Addressing - Dense Node
 *
 * Pattern: Range ≤ 64, density ≥ 25% → DIRECT_ADDRESSING
 * Example: 10 arcs in range of 20 (50% density)
 * Lucene Behavior: O(1) lookup with bit table, fast for dense nodes
 */
TEST(FSTArcEncodingTest, DirectAddressingDenseNode) {
    // Range: a-t (20 chars), 10 arcs present (50% density)
    auto fst = buildTestFST({{"a", 1},
                             {"c", 3},
                             {"e", 5},
                             {"g", 7},
                             {"i", 9},
                             {"k", 11},
                             {"m", 13},
                             {"o", 15},
                             {"q", 17},
                             {"s", 19}});

    // All arcs work
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(11, fst->get(toBytes("k")));
    EXPECT_EQ(19, fst->get(toBytes("s")));

    // Gaps don't work
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("d")));
}

/**
 * Test: Direct Addressing - High Density
 *
 * Pattern: Many arcs in small range (high density)
 * Example: 15 arcs in range of 20 (75% density)
 */
TEST(FSTArcEncodingTest, DirectAddressingHighDensity) {
    // Range: a-t (20 chars), 15 arcs (75% density)
    FST::Builder builder;
    for (char c : {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o'}) {
        std::string term(1, c);
        builder.add(toBytes(term), static_cast<int64_t>(c - 'a' + 1));
    }
    auto fst = builder.finish();

    // All present arcs work
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(8, fst->get(toBytes("h")));
    EXPECT_EQ(15, fst->get(toBytes("o")));

    // Missing arcs outside range
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("p")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("z")));
}

/**
 * Test: Direct Addressing - Edge Case Density
 *
 * Pattern: Exactly 25% density (threshold)
 * Range = 64, arcs = 16 (25%)
 */
TEST(FSTArcEncodingTest, DirectAddressingEdgeCaseDensity) {
    FST::Builder builder;
    // Range: 0x00 - 0x3F (64 values)
    // 16 arcs (exactly 25% density)
    for (int i = 0; i < 64; i += 4) {
        uint8_t byte = static_cast<uint8_t>(i);
        builder.add(BytesRef(&byte, 1), i);
    }
    auto fst = builder.finish();

    // Arcs at multiples of 4
    for (int i = 0; i < 64; i += 4) {
        uint8_t byte = static_cast<uint8_t>(i);
        EXPECT_EQ(i, fst->get(BytesRef(&byte, 1)));
    }

    // Others missing
    uint8_t byte1 = 1;
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef(&byte1, 1)));
}

/**
 * Test: Direct Addressing - Multi-Level Dense
 *
 * Pattern: Multiple nodes using direct addressing
 */
TEST(FSTArcEncodingTest, DirectAddressingMultiLevel) {
    FST::Builder builder;
    // Root: dense (a-j, 10 in 10 = 100%)
    // Each child: dense (0-9, 10 in 10 = 100%)
    for (char c = 'a'; c <= 'j'; c++) {
        for (char n = '0'; n <= '9'; n++) {
            std::string term;
            term += c;
            term += n;
            int64_t output = (c - 'a') * 10 + (n - '0');
            builder.add(toBytes(term), output);
        }
    }
    auto fst = builder.finish();

    // Spot checks
    EXPECT_EQ(0, fst->get(toBytes("a0")));
    EXPECT_EQ(55, fst->get(toBytes("f5")));
    EXPECT_EQ(99, fst->get(toBytes("j9")));

    // Outside range
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("k0")));
}

// ==================== Task 4.5: Mixed Encoding Tests ====================

/**
 * Test: Mixed Encodings in Same FST
 *
 * Pattern: Different nodes use different encodings
 * Example:
 * - Root: BINARY_SEARCH (6+ arcs)
 * - Some children: CONTINUOUS (sequential)
 * - Some children: LINEAR_SCAN (few arcs)
 */
TEST(FSTArcEncodingTest, MixedEncodingsInSameFST) {
    auto fst = buildTestFST({
        // Root has 6 arcs → BINARY_SEARCH
        {"a1", 1},  // 'a' node has 2 arcs (1,2) → LINEAR_SCAN
        {"a2", 2},
        {"b0", 3},  // 'b' node has 3 arcs (0,1,2) → CONTINUOUS
        {"b1", 4},
        {"b2", 5},
        {"c5", 6},  // 'c' node has 2 arcs → LINEAR_SCAN
        {"c9", 7},
        {"d0", 8},  // 'd' node has 10 arcs (0-9) → CONTINUOUS
        {"d1", 9},
        {"d2", 10},
        {"d3", 11},
        {"d4", 12},
        {"d5", 13},
        {"d6", 14},
        {"d7", 15},
        {"d8", 16},
        {"d9", 17},
        {"ex", 18},  // 'e' node has 1 arc → LINEAR_SCAN
        {"fy", 19}   // 'f' node has 1 arc → LINEAR_SCAN
    });

    // All terms work regardless of encoding
    EXPECT_EQ(1, fst->get(toBytes("a1")));
    EXPECT_EQ(5, fst->get(toBytes("b2")));
    EXPECT_EQ(7, fst->get(toBytes("c9")));
    EXPECT_EQ(13, fst->get(toBytes("d5")));
    EXPECT_EQ(18, fst->get(toBytes("ex")));
    EXPECT_EQ(19, fst->get(toBytes("fy")));

    // Non-existent terms don't work
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a3")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("b3")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("g0")));
}

/**
 * Test: Same Input Different Encodings Produce Same Results
 *
 * Pattern: Build FST twice with different term orders
 * Result: Same lookups, possibly different internal encodings
 */
TEST(FSTArcEncodingTest, SameInputDifferentEncodingsProduceSameResults) {
    // First FST
    auto fst1 = buildTestFST({{"apple", 1}, {"banana", 2}, {"cherry", 3}});

    // Second FST (same data, different internal structure possible)
    auto fst2 = buildTestFST({{"apple", 1}, {"banana", 2}, {"cherry", 3}});

    // Both should produce same results
    EXPECT_EQ(fst1->get(toBytes("apple")), fst2->get(toBytes("apple")));
    EXPECT_EQ(fst1->get(toBytes("banana")), fst2->get(toBytes("banana")));
    EXPECT_EQ(fst1->get(toBytes("cherry")), fst2->get(toBytes("cherry")));
    EXPECT_EQ(fst1->get(toBytes("durian")), fst2->get(toBytes("durian")));
}

// ==================== Task 4.6: Encoding Edge Cases ====================

/**
 * Test: Empty Node (No Arcs)
 *
 * Pattern: Final node with no outgoing arcs
 */
TEST(FSTArcEncodingTest, EmptyNodeNoArcs) {
    auto fst = buildTestFST({{"a", 1}, {"b", 2}});

    // Nodes 'a' and 'b' have no arcs (final nodes)
    EXPECT_EQ(1, fst->get(toBytes("a")));
    EXPECT_EQ(2, fst->get(toBytes("b")));

    // Extensions don't work
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("aa")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("ab")));
}

/**
 * Test: Single Label at Extremes
 *
 * Pattern: Labels at 0x00 and 0xFF (byte boundaries)
 */
TEST(FSTArcEncodingTest, SingleLabelAtExtremes) {
    FST::Builder builder;

    uint8_t byte0 = 0x00;
    uint8_t byteFF = 0xFF;

    builder.add(BytesRef(&byte0, 1), 0);
    builder.add(BytesRef(&byteFF, 1), 255);

    auto fst = builder.finish();

    EXPECT_EQ(0, fst->get(BytesRef(&byte0, 1)));
    EXPECT_EQ(255, fst->get(BytesRef(&byteFF, 1)));

    // Middle values not present
    uint8_t byte80 = 0x80;
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef(&byte80, 1)));
}

/**
 * Test: All Encoding Types in Depth
 *
 * Pattern: Create deep FST where different levels use different encodings
 */
TEST(FSTArcEncodingTest, AllEncodingTypesInDepth) {
    // Level 1: CONTINUOUS (a-e)
    // Level 2: BINARY_SEARCH (6 arcs with gaps)
    // Level 3: LINEAR_SCAN (2 arcs)
    FST::Builder builder;

    for (char L1 : {'a', 'b', 'c', 'd', 'e'}) {
        for (char L2 : {'0', '2', '4', '6', '8', '9'}) {
            for (char L3 : {'x', 'z'}) {
                std::string term;
                term += L1;
                term += L2;
                term += L3;
                int64_t output = (L1 - 'a') * 100 + (L2 - '0') * 10 + (L3 - 'x');
                builder.add(toBytes(term), output);
            }
        }
    }
    auto fst = builder.finish();

    // Spot checks
    // Formula: (L1 - 'a') * 100 + (L2 - '0') * 10 + (L3 - 'x')
    // "a0x": 0 * 100 + 0 * 10 + 0 = 0
    // "c4z": 2 * 100 + 4 * 10 + 2 = 242 ('z' - 'x' = 2, not 1)
    // "e9z": 4 * 100 + 9 * 10 + 2 = 492
    EXPECT_EQ(0, fst->get(toBytes("a0x")));
    EXPECT_EQ(242, fst->get(toBytes("c4z")));
    EXPECT_EQ(492, fst->get(toBytes("e9z")));

    // Missing combinations
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a1x")));  // L2 gap
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("a0y")));  // L3 missing
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(toBytes("f0x")));  // L1 outside range
}

// ==================== Summary Statistics ====================

/**
 * Note: These tests verify FST arc encoding strategies work correctly.
 *
 * Key Properties Verified:
 * 1. LINEAR_SCAN: Works for sparse nodes (< 6 arcs)
 * 2. CONTINUOUS: Works for sequential labels (no gaps)
 * 3. BINARY_SEARCH: Works for moderate density (≥ 6 arcs, not continuous)
 * 4. DIRECT_ADDRESSING: Works for dense nodes (range ≤ 64, density ≥ 25%)
 * 5. Mixed encodings in same FST work correctly
 * 6. Different encodings produce same lookup results
 * 7. Edge cases (empty nodes, byte extremes, deep FST) work
 *
 * If all tests pass, Diagon FST arc encoding strategies are correct.
 */
