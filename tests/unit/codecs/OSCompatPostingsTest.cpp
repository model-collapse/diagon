// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * OS-Compat Postings Tests (Wave 3)
 *
 * Tests for ForUtil, PForUtil, and the OS-compat postings writer/reader.
 *
 * Test structure:
 * 1. ForUtil: encode→decode round-trip for various bit widths (1-31)
 * 2. PForUtil: PFOR encode→decode with exception patterns
 * 3. Postings writer/reader: full round-trip for .doc/.pos files
 */

#include <gtest/gtest.h>

#include "diagon/codecs/lucene104/ForUtil.h"
#include "diagon/codecs/lucene104/PForUtil.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsWriter.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsReader.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <cstdlib>
#include <cstring>
#include <random>

using namespace diagon;
using namespace diagon::codecs::lucene104;
using namespace diagon::store;

// ==================== ForUtil Tests ====================

class ForUtilTest : public ::testing::Test {
protected:
    ForUtil forUtil_;
};

TEST_F(ForUtilTest, RoundTripBitsPerValue1) {
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    // bpv=1: all values 0 or 1
    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = i % 2;
        encodeBuf[i] = original[i];
    }

    ByteBuffersIndexOutput out("test");
    forUtil_.encode(encodeBuf, 1, out);

    EXPECT_EQ(out.size(), static_cast<size_t>(ForUtil::numBytes(1)));

    ByteBuffersIndexInput in("test", out.toArrayCopy());
    forUtil_.decode(1, in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i << " for bpv=1";
    }
}

TEST_F(ForUtilTest, RoundTripAllBitWidths) {
    std::mt19937 rng(42);

    for (int bpv = 1; bpv <= 31; ++bpv) {
        int32_t original[ForUtil::BLOCK_SIZE];
        int32_t decoded[ForUtil::BLOCK_SIZE];
        int32_t encodeBuf[ForUtil::BLOCK_SIZE];

        int32_t maxVal = (bpv == 31) ? 0x7FFFFFFF : (1 << bpv) - 1;
        std::uniform_int_distribution<int32_t> dist(0, maxVal);

        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            original[i] = dist(rng);
            encodeBuf[i] = original[i];
        }

        ByteBuffersIndexOutput out("test_bpv" + std::to_string(bpv));
        forUtil_.encode(encodeBuf, bpv, out);

        EXPECT_EQ(out.size(), static_cast<size_t>(ForUtil::numBytes(bpv)))
            << "Wrong byte count for bpv=" << bpv;

        ByteBuffersIndexInput in("test_bpv" + std::to_string(bpv), out.toArrayCopy());
        forUtil_.decode(bpv, in, decoded);

        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            EXPECT_EQ(decoded[i], original[i])
                << "Mismatch at index " << i << " for bpv=" << bpv;
        }
    }
}

TEST_F(ForUtilTest, RoundTripBpv8Boundary) {
    // bpv=8: exactly fits 4 values per int32 word
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = i & 0xFF;  // 0-255
        encodeBuf[i] = original[i];
    }

    ByteBuffersIndexOutput out("test_bpv8");
    forUtil_.encode(encodeBuf, 8, out);

    ByteBuffersIndexInput in("test_bpv8", out.toArrayCopy());
    forUtil_.decode(8, in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST_F(ForUtilTest, RoundTripBpv16Boundary) {
    // bpv=16: exactly fits 2 values per int32 word
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = (i * 257) & 0xFFFF;
        encodeBuf[i] = original[i];
    }

    ByteBuffersIndexOutput out("test_bpv16");
    forUtil_.encode(encodeBuf, 16, out);

    ByteBuffersIndexInput in("test_bpv16", out.toArrayCopy());
    forUtil_.decode(16, in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST_F(ForUtilTest, AllZeros) {
    // bpv=1 with all zeros
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    std::memset(original, 0, sizeof(original));
    std::memset(encodeBuf, 0, sizeof(encodeBuf));

    ByteBuffersIndexOutput out("test_zeros");
    forUtil_.encode(encodeBuf, 1, out);

    ByteBuffersIndexInput in("test_zeros", out.toArrayCopy());
    forUtil_.decode(1, in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], 0) << "Expected 0 at index " << i;
    }
}

TEST_F(ForUtilTest, MaxValues) {
    // bpv=8 with all 255s
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        encodeBuf[i] = 255;
    }

    ByteBuffersIndexOutput out("test_max");
    forUtil_.encode(encodeBuf, 8, out);

    ByteBuffersIndexInput in("test_max", out.toArrayCopy());
    forUtil_.decode(8, in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], 255) << "Expected 255 at index " << i;
    }
}

// ==================== PForUtil Tests ====================

class PForUtilTest : public ::testing::Test {
protected:
    ForUtil forUtil_;
    PForUtil pforUtil_{forUtil_};
};

TEST_F(PForUtilTest, RoundTripNoExceptions) {
    // All values fit in the same bit width — no exceptions needed
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = i & 0x7F;  // 0-127, all fit in 7 bits
        encodeBuf[i] = original[i];
    }

    ByteBuffersIndexOutput out("test_pfor_no_exc");
    pforUtil_.encode(encodeBuf, out);

    ByteBuffersIndexInput in("test_pfor_no_exc", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST_F(PForUtilTest, RoundTripWithExceptions) {
    // Most values small, a few outliers that become exceptions
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = i & 0x0F;  // 0-15, fit in 4 bits
        encodeBuf[i] = original[i];
    }

    // Add a few exceptions (values needing more bits)
    original[10] = 500;  encodeBuf[10] = 500;   // needs ~9 bits
    original[100] = 1000; encodeBuf[100] = 1000; // needs 10 bits
    original[200] = 2000; encodeBuf[200] = 2000; // needs 11 bits

    ByteBuffersIndexOutput out("test_pfor_exc");
    pforUtil_.encode(encodeBuf, out);

    ByteBuffersIndexInput in("test_pfor_exc", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST_F(PForUtilTest, RoundTripAllEqual) {
    // All values the same — should use all-equal special case
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        encodeBuf[i] = 42;
    }

    ByteBuffersIndexOutput out("test_pfor_equal");
    pforUtil_.encode(encodeBuf, out);

    // All-equal case: 1 byte token + VInt(42) = very compact
    EXPECT_LT(out.size(), 10u) << "All-equal should be very compact";

    ByteBuffersIndexInput in("test_pfor_equal", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], 42) << "Expected 42 at index " << i;
    }
}

TEST_F(PForUtilTest, RoundTripAllZeros) {
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    std::memset(original, 0, sizeof(original));
    std::memset(encodeBuf, 0, sizeof(encodeBuf));

    ByteBuffersIndexOutput out("test_pfor_zeros");
    pforUtil_.encode(encodeBuf, out);

    ByteBuffersIndexInput in("test_pfor_zeros", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], 0) << "Expected 0 at index " << i;
    }
}

TEST_F(PForUtilTest, RoundTripMax7Exceptions) {
    // Exercise the max exception count
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = 1;  // All 1s, fit in 1 bit
        encodeBuf[i] = 1;
    }

    // Add exactly 7 exceptions (max)
    for (int i = 0; i < 7; ++i) {
        original[i * 30] = 300;  // needs ~9 bits
        encodeBuf[i * 30] = 300;
    }

    ByteBuffersIndexOutput out("test_pfor_7exc");
    pforUtil_.encode(encodeBuf, out);

    ByteBuffersIndexInput in("test_pfor_7exc", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST_F(PForUtilTest, RoundTripRandomValues) {
    std::mt19937 rng(12345);

    for (int trial = 0; trial < 10; ++trial) {
        int32_t original[ForUtil::BLOCK_SIZE];
        int32_t decoded[ForUtil::BLOCK_SIZE];
        int32_t encodeBuf[ForUtil::BLOCK_SIZE];

        // Mostly small values with a few large ones
        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            if (rng() % 50 == 0) {
                original[i] = rng() % 65536;  // occasional large value
            } else {
                original[i] = rng() % 256;    // mostly small
            }
            encodeBuf[i] = original[i];
        }

        ByteBuffersIndexOutput out("test_pfor_random_" + std::to_string(trial));
        pforUtil_.encode(encodeBuf, out);

        ByteBuffersIndexInput in("test_pfor_random_" + std::to_string(trial), out.toArrayCopy());
        pforUtil_.decode(in, decoded);

        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            EXPECT_EQ(decoded[i], original[i])
                << "Mismatch at index " << i << " trial " << trial;
        }
    }
}

TEST_F(PForUtilTest, SkipMatchesDecode) {
    // Verify that skip() advances the input to the same position as decode()
    int32_t values[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];

    std::mt19937 rng(999);
    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        values[i] = rng() % 1024;
    }

    ByteBuffersIndexOutput out("test_skip");
    pforUtil_.encode(values, out);

    // Write a marker after the encoded block
    out.writeInt(0xDEADBEEF);

    auto bytes = out.toArrayCopy();

    // Decode path
    ByteBuffersIndexInput in1("test_skip_decode", bytes);
    pforUtil_.decode(in1, decoded);
    int64_t posAfterDecode = in1.getFilePointer();

    // Skip path
    ByteBuffersIndexInput in2("test_skip_skip", bytes);
    PForUtil::skip(in2);
    int64_t posAfterSkip = in2.getFilePointer();

    EXPECT_EQ(posAfterSkip, posAfterDecode)
        << "skip() should advance to same position as decode()";

    // Verify the marker is readable after both
    EXPECT_EQ(in2.readInt(), static_cast<int32_t>(0xDEADBEEF));
}

TEST_F(PForUtilTest, RoundTripFrequencyLikeValues) {
    // Simulate frequency data: mostly 1s with a few larger values
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = 1;  // Most terms appear once per doc
        encodeBuf[i] = 1;
    }
    // A few high-frequency terms
    original[5] = 15;   encodeBuf[5] = 15;
    original[50] = 100;  encodeBuf[50] = 100;
    original[150] = 7;   encodeBuf[150] = 7;

    ByteBuffersIndexOutput out("test_pfor_freq");
    pforUtil_.encode(encodeBuf, out);

    ByteBuffersIndexInput in("test_pfor_freq", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST_F(PForUtilTest, RoundTripDocDeltaLikeValues) {
    // Simulate doc delta data: small increments (typically 1-10)
    int32_t original[ForUtil::BLOCK_SIZE];
    int32_t decoded[ForUtil::BLOCK_SIZE];
    int32_t encodeBuf[ForUtil::BLOCK_SIZE];

    std::mt19937 rng(7777);
    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        original[i] = 1 + (rng() % 5);  // deltas of 1-5
        encodeBuf[i] = original[i];
    }

    ByteBuffersIndexOutput out("test_pfor_delta");
    pforUtil_.encode(encodeBuf, out);

    ByteBuffersIndexInput in("test_pfor_delta", out.toArrayCopy());
    pforUtil_.decode(in, decoded);

    for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
        EXPECT_EQ(decoded[i], original[i]) << "Mismatch at index " << i;
    }
}
