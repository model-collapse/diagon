// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include <gtest/gtest.h>

#include "diagon/codecs/lucene90/Lucene90BlockTreeTermsReader.h"
#include "diagon/codecs/lucene90/Lucene90PostingsFormat.h"
#include "diagon/codecs/lucene90/Lucene90PostingsReader.h"
#include "diagon/codecs/lucene90/LuceneFST.h"

#include <cstdint>
#include <vector>

using namespace diagon::codecs::lucene90;

namespace {

// ==================== MSB VLong Round-Trip ====================

TEST(Lucene90BlockTreeTest, MSBVLongRoundTrip) {
    // Write MSB VLong encoding for known values and verify decoding
    auto encodeMSBVLong = [](int64_t val) -> std::vector<uint8_t> {
        if (val == 0) return {0x00};
        std::vector<uint8_t> bytes;
        // Count how many 7-bit groups we need
        int64_t v = val;
        int numBytes = 0;
        while (v > 0) {
            v >>= 7;
            numBytes++;
        }
        // Encode MSB first
        for (int i = numBytes - 1; i >= 0; --i) {
            uint8_t b = static_cast<uint8_t>((val >> (i * 7)) & 0x7F);
            if (i > 0) b |= 0x80;
            bytes.push_back(b);
        }
        return bytes;
    };

    auto testValue = [&](int64_t val) {
        auto encoded = encodeMSBVLong(val);
        size_t pos = 0;
        int64_t decoded = LuceneFST::readMSBVLong(encoded.data(), pos);
        EXPECT_EQ(decoded, val) << "Failed for value " << val;
    };

    testValue(0);
    testValue(1);
    testValue(127);
    testValue(128);
    testValue(255);
    testValue(16383);
    testValue(16384);
    testValue(1000000);
    testValue(INT64_C(1) << 32);
}

// ==================== BlockTree Output Flags ====================

TEST(Lucene90BlockTreeTest, OutputFlagDecoding) {
    // Verify OUTPUT_FLAG_IS_FLOOR and OUTPUT_FLAG_HAS_TERMS
    EXPECT_EQ(OUTPUT_FLAG_IS_FLOOR, 0x1);
    EXPECT_EQ(OUTPUT_FLAG_HAS_TERMS, 0x2);
    EXPECT_EQ(OUTPUT_FLAGS_NUM_BITS, 2);
    EXPECT_EQ(OUTPUT_FLAGS_MASK, 0x3);

    // FP=100, hasTerms=true, isFloor=false → code = (100 << 2) | 0x2 = 402
    int64_t code = (100LL << OUTPUT_FLAGS_NUM_BITS) | OUTPUT_FLAG_HAS_TERMS;
    EXPECT_EQ(code, 402);
    EXPECT_EQ(code >> OUTPUT_FLAGS_NUM_BITS, 100);
    EXPECT_EQ(code & OUTPUT_FLAG_IS_FLOOR, 0);
    EXPECT_NE(code & OUTPUT_FLAG_HAS_TERMS, 0);

    // FP=50, both flags → code = (50 << 2) | 0x3 = 203
    code = (50LL << OUTPUT_FLAGS_NUM_BITS) | OUTPUT_FLAG_IS_FLOOR | OUTPUT_FLAG_HAS_TERMS;
    EXPECT_EQ(code, 203);
    EXPECT_EQ(code >> OUTPUT_FLAGS_NUM_BITS, 50);
    EXPECT_NE(code & OUTPUT_FLAG_IS_FLOOR, 0);
    EXPECT_NE(code & OUTPUT_FLAG_HAS_TERMS, 0);
}

// ==================== FieldReaderMeta defaults ====================

TEST(Lucene90BlockTreeTest, FieldReaderMetaDefaults) {
    FieldReaderMeta meta;
    EXPECT_EQ(meta.fieldNumber, 0);
    EXPECT_EQ(meta.numTerms, 0);
    EXPECT_TRUE(meta.rootCode.empty());
    EXPECT_EQ(meta.sumTotalTermFreq, 0);
    EXPECT_EQ(meta.sumDocFreq, 0);
    EXPECT_EQ(meta.docCount, 0);
    EXPECT_TRUE(meta.minTerm.empty());
    EXPECT_TRUE(meta.maxTerm.empty());
    EXPECT_EQ(meta.indexStartFP, 0);
}

// ==================== Lucene90TermState defaults ====================

TEST(Lucene90BlockTreeTest, TermStateDefaults) {
    Lucene90TermState ts;
    EXPECT_EQ(ts.docStartFP, 0);
    EXPECT_EQ(ts.posStartFP, -1);
    EXPECT_EQ(ts.payStartFP, -1);
    EXPECT_EQ(ts.docFreq, 0);
    EXPECT_EQ(ts.totalTermFreq, 0);
    EXPECT_EQ(ts.skipOffset, -1);
    EXPECT_EQ(ts.lastPosBlockOffset, -1);
    EXPECT_EQ(ts.singletonDocID, -1);
}

// ==================== Lucene90TermsFrame reset ====================

TEST(Lucene90BlockTreeTest, FrameReset) {
    Lucene90TermsFrame frame;
    frame.fp = 42;
    frame.entCount = 10;
    frame.isLeafBlock = true;
    frame.termState.docFreq = 100;

    frame.reset();

    EXPECT_EQ(frame.fp, 0);
    EXPECT_EQ(frame.entCount, 0);
    EXPECT_FALSE(frame.isLeafBlock);
    EXPECT_EQ(frame.termState.docFreq, 0);
    EXPECT_EQ(frame.termState.docStartFP, 0);
    EXPECT_EQ(frame.termState.singletonDocID, -1);
}

// ==================== BlockTree Version Constants ====================

TEST(Lucene90BlockTreeTest, VersionConstants) {
    EXPECT_EQ(BLOCKTREE_VERSION_START, 0);
    EXPECT_EQ(BLOCKTREE_VERSION_MSB_VLONG, 1);
    EXPECT_EQ(BLOCKTREE_VERSION_CONTINUOUS_ARCS, 2);
    EXPECT_EQ(BLOCKTREE_VERSION_CURRENT, BLOCKTREE_VERSION_CONTINUOUS_ARCS);
}

// ==================== Lucene90PostingsFormat name ====================

TEST(Lucene90BlockTreeTest, PostingsFormatName) {
    Lucene90PostingsFormat fmt;
    EXPECT_EQ(fmt.getName(), "Lucene90");
}

TEST(Lucene90BlockTreeTest, PostingsFormatWriteThrows) {
    Lucene90PostingsFormat fmt;
    // fieldsConsumer should throw (read-only)
    diagon::index::FieldInfos fis;
    diagon::index::SegmentWriteState state(nullptr, "test", 0, fis, "");
    EXPECT_THROW(fmt.fieldsConsumer(state), std::runtime_error);
}

}  // namespace
