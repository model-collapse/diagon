// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include <gtest/gtest.h>

#include "diagon/codecs/lucene90/LuceneFST.h"

#include <cstdint>
#include <vector>

using namespace diagon::codecs::lucene90;

namespace {

// ==================== MSB VLong Tests ====================

TEST(LuceneFSTTest, MSBVLongSingleByte) {
    // Single byte: value 42 = 0x2A (no continuation bit)
    std::vector<uint8_t> data = {42};
    size_t pos = 0;
    EXPECT_EQ(LuceneFST::readMSBVLong(data.data(), pos), 42);
    EXPECT_EQ(pos, 1u);
}

TEST(LuceneFSTTest, MSBVLongTwoBytes) {
    // Two bytes: high byte has continuation bit set
    // Value = (0x01 << 7) | 0x00 = 128
    std::vector<uint8_t> data = {0x81, 0x00};
    size_t pos = 0;
    EXPECT_EQ(LuceneFST::readMSBVLong(data.data(), pos), 128);
    EXPECT_EQ(pos, 2u);
}

TEST(LuceneFSTTest, MSBVLongThreeBytes) {
    // Value 16384 = 0x4000 = (0x01 << 14) | (0x00 << 7) | 0x00
    std::vector<uint8_t> data = {0x81, 0x80, 0x00};
    size_t pos = 0;
    EXPECT_EQ(LuceneFST::readMSBVLong(data.data(), pos), 16384);
    EXPECT_EQ(pos, 3u);
}

TEST(LuceneFSTTest, MSBVLongKnownValue) {
    // Encode value 300 = 0x12C in MSB VLong
    // 300 in binary: 100101100
    // Split into 7-bit groups MSB first: 0000010 0101100
    // Encoded: 0x82, 0x2C
    std::vector<uint8_t> data = {0x82, 0x2C};
    size_t pos = 0;
    EXPECT_EQ(LuceneFST::readMSBVLong(data.data(), pos), 300);
    EXPECT_EQ(pos, 2u);
}

TEST(LuceneFSTTest, MSBVLongZero) {
    std::vector<uint8_t> data = {0x00};
    size_t pos = 0;
    EXPECT_EQ(LuceneFST::readMSBVLong(data.data(), pos), 0);
    EXPECT_EQ(pos, 1u);
}

// ==================== FST Construction from Bytes ====================

TEST(LuceneFSTTest, ConstructFromBytes) {
    // Simple test: construct FST from pre-built byte array
    // Start node at position 5, version 10
    std::vector<uint8_t> bytes(10, 0);
    LuceneFST fst(5, 10, bytes);
    EXPECT_EQ(fst.version(), 10);
}

TEST(LuceneFSTTest, GetFirstArcWithEmptyOutput) {
    std::vector<uint8_t> emptyOutput = {0x0A};  // some root code
    std::vector<uint8_t> bytes(10, 0);
    LuceneFST fst(5, 10, bytes, emptyOutput);

    LuceneFST::Arc arc;
    fst.getFirstArc(arc);

    EXPECT_TRUE(arc.isFinal());
    EXPECT_EQ(arc.target, 5);
    EXPECT_EQ(arc.nextFinalOutput, emptyOutput);
}

TEST(LuceneFSTTest, GetFirstArcWithoutEmptyOutput) {
    std::vector<uint8_t> bytes(10, 0);
    LuceneFST fst(5, 10, bytes);

    LuceneFST::Arc arc;
    fst.getFirstArc(arc);

    EXPECT_FALSE(arc.isFinal());
    EXPECT_EQ(arc.target, 5);
    EXPECT_TRUE(arc.nextFinalOutput.empty());
}

// ==================== Linear Scan Node ====================

TEST(LuceneFSTTest, LinearScanSingleArc) {
    // Build a minimal linear-scan FST with one arc at the root
    // Pad with a leading byte so startNode != 0 (0 = NON_FINAL_END_NODE)
    // Node at position 1: flags byte + label (STOP_NODE means no target VLong)
    uint8_t flags = LuceneFST::BIT_LAST_ARC | LuceneFST::BIT_STOP_NODE | LuceneFST::BIT_FINAL_ARC;
    uint8_t label = 'a';

    std::vector<uint8_t> bytes = {0x00, flags, label};  // pad byte, then arc
    int64_t startNode = 1;  // Must be > 0 (0 = NON_FINAL_END_NODE)

    LuceneFST fst(startNode, 10, bytes);

    LuceneFST::Arc root;
    fst.getFirstArc(root);

    LuceneFST::Arc found;
    EXPECT_TRUE(fst.findTargetArc('a', root, found));
    EXPECT_EQ(found.label, 'a');
    EXPECT_EQ(found.target, LuceneFST::FINAL_END_NODE);
}

TEST(LuceneFSTTest, LinearScanArcNotFound) {
    uint8_t flags = LuceneFST::BIT_LAST_ARC | LuceneFST::BIT_STOP_NODE | LuceneFST::BIT_FINAL_ARC;
    uint8_t label = 'a';

    std::vector<uint8_t> bytes = {0x00, flags, label};
    LuceneFST fst(1, 10, bytes);

    LuceneFST::Arc root;
    fst.getFirstArc(root);

    LuceneFST::Arc found;
    EXPECT_FALSE(fst.findTargetArc('b', root, found));
}

// ==================== END_LABEL Tests ====================

TEST(LuceneFSTTest, EndLabelOnFinalArc) {
    // A final arc should accept END_LABEL
    LuceneFST::Arc follow;
    follow.flags = LuceneFST::BIT_FINAL_ARC;
    follow.nextFinalOutput = {0x42};

    std::vector<uint8_t> bytes(4, 0);
    LuceneFST fst(0, 10, bytes);

    LuceneFST::Arc arc;
    EXPECT_TRUE(fst.findTargetArc(LuceneFST::END_LABEL, follow, arc));
    EXPECT_EQ(arc.label, LuceneFST::END_LABEL);
    EXPECT_EQ(arc.target, LuceneFST::FINAL_END_NODE);
    EXPECT_EQ(arc.output, std::vector<uint8_t>({0x42}));
}

TEST(LuceneFSTTest, EndLabelOnNonFinalArc) {
    LuceneFST::Arc follow;
    follow.flags = 0;  // Not final

    std::vector<uint8_t> bytes(4, 0);
    LuceneFST fst(0, 10, bytes);

    LuceneFST::Arc arc;
    EXPECT_FALSE(fst.findTargetArc(LuceneFST::END_LABEL, follow, arc));
}

// ==================== TargetHasArcs ====================

TEST(LuceneFSTTest, TargetHasArcs) {
    LuceneFST::Arc arc;
    arc.target = 42;
    EXPECT_TRUE(LuceneFST::targetHasArcs(arc));

    arc.target = LuceneFST::FINAL_END_NODE;  // -1
    EXPECT_FALSE(LuceneFST::targetHasArcs(arc));

    arc.target = LuceneFST::NON_FINAL_END_NODE;  // 0
    EXPECT_FALSE(LuceneFST::targetHasArcs(arc));
}

}  // namespace
