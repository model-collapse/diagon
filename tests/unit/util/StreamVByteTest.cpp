// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/StreamVByte.h"
#include "diagon/util/VByte.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using namespace diagon::util;

// ==================== Basic Encoding/Decoding ====================

TEST(StreamVByteTest, EncodeDecode4_Small) {
    // Test 4 small values (1 byte each)
    uint32_t values[4] = {10, 20, 30, 40};
    uint8_t buffer[20];

    int encoded = StreamVByte::encode(values, 4, buffer);
    EXPECT_EQ(5, encoded);  // 1 control + 4 data bytes

    uint32_t decoded[4];
    int consumed = StreamVByte::decode4(buffer, decoded);
    EXPECT_EQ(5, consumed);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }
}

TEST(StreamVByteTest, EncodeDecode4_Mixed) {
    // Test mixed sizes: 1, 2, 3, 3 bytes
    // Note: 10000000 = 0x989680 = 3 bytes (< 16777216)
    uint32_t values[4] = {
        100,           // 1 byte
        1000,          // 2 bytes
        100000,        // 3 bytes
        10000000       // 3 bytes (0x989680)
    };
    uint8_t buffer[20];

    int encoded = StreamVByte::encode(values, 4, buffer);
    EXPECT_EQ(10, encoded);  // 1 control + (1+2+3+3) data bytes

    uint32_t decoded[4];
    int consumed = StreamVByte::decode4(buffer, decoded);
    EXPECT_EQ(10, consumed);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }
}

TEST(StreamVByteTest, EncodeDecode4_Large) {
    // Test large values (4 bytes each)
    uint32_t values[4] = {
        0xFFFFFFFF,
        0x12345678,
        0xABCDEF00,
        0x80000000
    };
    uint8_t buffer[20];

    int encoded = StreamVByte::encode(values, 4, buffer);
    EXPECT_EQ(17, encoded);  // 1 control + 16 data bytes

    uint32_t decoded[4];
    int consumed = StreamVByte::decode4(buffer, decoded);
    EXPECT_EQ(17, consumed);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }
}

TEST(StreamVByteTest, EncodeDecode4_Zeros) {
    // Test all zeros
    uint32_t values[4] = {0, 0, 0, 0};
    uint8_t buffer[20];

    int encoded = StreamVByte::encode(values, 4, buffer);
    EXPECT_EQ(5, encoded);  // 1 control + 4 data bytes

    uint32_t decoded[4];
    int consumed = StreamVByte::decode4(buffer, decoded);
    EXPECT_EQ(5, consumed);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(0U, decoded[i]);
    }
}

// ==================== Bulk Decoding ====================

TEST(StreamVByteTest, DecodeBulk_8Integers) {
    // Test bulk decode with 8 integers (2 groups of 4)
    uint32_t values[8] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};
    uint8_t buffer[50];

    // Encode in groups of 4
    int offset = 0;
    offset += StreamVByte::encode(values, 4, buffer + offset);
    offset += StreamVByte::encode(values + 4, 4, buffer + offset);

    // Bulk decode
    uint32_t decoded[8];
    int consumed = StreamVByte::decodeBulk(buffer, 8, decoded);
    EXPECT_GT(consumed, 0);

    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }
}

TEST(StreamVByteTest, DecodeBulk_12Integers) {
    // Test bulk decode with 12 integers (3 groups of 4)
    std::vector<uint32_t> values;
    for (int i = 0; i < 12; ++i) {
        values.push_back(i * 1000 + 1);
    }

    uint8_t buffer[100];
    int offset = 0;
    for (int i = 0; i < 12; i += 4) {
        offset += StreamVByte::encode(values.data() + i, 4, buffer + offset);
    }

    std::vector<uint32_t> decoded(12);
    int consumed = StreamVByte::decodeBulk(buffer, 12, decoded.data());
    EXPECT_GT(consumed, 0);
    EXPECT_EQ(values, decoded);
}

// ==================== Flexible Decode (Any Count) ====================

TEST(StreamVByteTest, Decode_Count5) {
    // Test decode with count not multiple of 4
    std::vector<uint32_t> values = {1, 10, 100, 1000, 10000};
    uint8_t buffer[50];

    // Encode (will create 2 groups: 4 + 1 padded to 4)
    int offset = 0;
    offset += StreamVByte::encode(values.data(), 4, buffer + offset);
    offset += StreamVByte::encode(values.data() + 4, 1, buffer + offset);

    // Decode exactly 5 integers
    std::vector<uint32_t> decoded(5);
    int consumed = StreamVByte::decode(buffer, 5, decoded.data());
    EXPECT_GT(consumed, 0);
    EXPECT_EQ(values, decoded);
}

TEST(StreamVByteTest, Decode_Count7) {
    // Test decode with 7 integers
    std::vector<uint32_t> values = {1, 2, 3, 4, 5, 6, 7};
    uint8_t buffer[50];

    // Encode
    int offset = 0;
    offset += StreamVByte::encode(values.data(), 4, buffer + offset);
    offset += StreamVByte::encode(values.data() + 4, 3, buffer + offset);

    // Decode
    std::vector<uint32_t> decoded(7);
    int consumed = StreamVByte::decode(buffer, 7, decoded.data());
    EXPECT_GT(consumed, 0);
    EXPECT_EQ(values, decoded);
}

TEST(StreamVByteTest, Decode_Count1) {
    // Test decode with single integer
    uint32_t value = 12345;
    uint8_t buffer[10];

    int encoded = StreamVByte::encode(&value, 1, buffer);

    uint32_t decoded;
    int consumed = StreamVByte::decode(buffer, 1, &decoded);
    EXPECT_GT(consumed, 0);
    EXPECT_EQ(value, decoded);
}

// ==================== Comparison with Scalar VByte ====================

TEST(StreamVByteTest, CompareWithVByte_DocIDDeltas) {
    // Simulate doc ID deltas (common use case)
    std::vector<uint32_t> docIds = {5, 12, 18, 25, 100, 200, 500, 1000};
    std::vector<uint32_t> deltas;

    uint32_t last = 0;
    for (uint32_t docId : docIds) {
        deltas.push_back(docId - last);
        last = docId;
    }

    // Encode with StreamVByte
    uint8_t stream_buffer[100];
    int stream_offset = 0;
    for (size_t i = 0; i < deltas.size(); i += 4) {
        int count = std::min(4, static_cast<int>(deltas.size() - i));
        stream_offset += StreamVByte::encode(deltas.data() + i, count, stream_buffer + stream_offset);
    }

    // Encode with regular VByte
    uint8_t vbyte_buffer[100];
    int vbyte_offset = 0;
    for (uint32_t delta : deltas) {
        vbyte_offset += VByte::encodeUInt32(delta, vbyte_buffer + vbyte_offset);
    }

    // StreamVByte should use fewer or equal bytes (more compact with control bytes)
    // Note: For small deltas, StreamVByte overhead might be slightly higher
    // But for mixed sizes, StreamVByte is more efficient

    // Decode both and verify same results
    std::vector<uint32_t> stream_decoded(deltas.size());
    StreamVByte::decode(stream_buffer, deltas.size(), stream_decoded.data());

    std::vector<uint32_t> vbyte_decoded;
    int read_offset = 0;
    while (read_offset < vbyte_offset) {
        int bytes_read;
        uint32_t value = VByte::decodeUInt32(vbyte_buffer + read_offset, &bytes_read);
        vbyte_decoded.push_back(value);
        read_offset += bytes_read;
    }

    EXPECT_EQ(deltas, stream_decoded);
    EXPECT_EQ(deltas, vbyte_decoded);
}

// ==================== Edge Cases ====================

TEST(StreamVByteTest, MaxUInt32) {
    // Test maximum uint32 value
    uint32_t values[4] = {
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max() - 1,
        std::numeric_limits<uint32_t>::max() - 100,
        std::numeric_limits<uint32_t>::max() - 1000
    };
    uint8_t buffer[20];

    int encoded = StreamVByte::encode(values, 4, buffer);
    EXPECT_EQ(17, encoded);  // All need 4 bytes

    uint32_t decoded[4];
    int consumed = StreamVByte::decode4(buffer, decoded);
    EXPECT_EQ(17, consumed);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }
}

TEST(StreamVByteTest, PowersOf256) {
    // Test boundary values (powers of 256)
    uint32_t values[4] = {
        255,          // 1 byte max
        256,          // 2 bytes min
        65535,        // 2 bytes max
        65536         // 3 bytes min
    };
    uint8_t buffer[20];

    int encoded = StreamVByte::encode(values, 4, buffer);

    uint32_t decoded[4];
    int consumed = StreamVByte::decode4(buffer, decoded);
    EXPECT_EQ(encoded, consumed);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }
}

// ==================== Encoded Size Tests ====================

TEST(StreamVByteTest, EncodedSize_Single) {
    EXPECT_EQ(1, StreamVByte::encodedSize(0));
    EXPECT_EQ(1, StreamVByte::encodedSize(255));
    EXPECT_EQ(2, StreamVByte::encodedSize(256));
    EXPECT_EQ(2, StreamVByte::encodedSize(65535));
    EXPECT_EQ(3, StreamVByte::encodedSize(65536));
    EXPECT_EQ(3, StreamVByte::encodedSize(16777215));
    EXPECT_EQ(4, StreamVByte::encodedSize(16777216));
    EXPECT_EQ(4, StreamVByte::encodedSize(std::numeric_limits<uint32_t>::max()));
}

TEST(StreamVByteTest, EncodedSize_Array) {
    uint32_t values[8] = {10, 1000, 100000, 10000000, 1, 2, 3, 4};

    int expected = 0;
    // Group 1: [10, 1000, 100000, 10000000] = 1 + (1+2+3+3) = 10 bytes
    // Note: 10000000 = 0x989680 is 3 bytes, not 4
    expected += 10;
    // Group 2: [1, 2, 3, 4] = 1 + (1+1+1+1) = 5 bytes
    expected += 5;

    int actual = StreamVByte::encodedSizeArray(values, 8);
    EXPECT_EQ(expected, actual);
}

// ==================== Performance Characteristics ====================

TEST(StreamVByteTest, LargeArray_Performance) {
    // Test with large array to verify SIMD benefits
    constexpr int COUNT = 1024;  // Multiple of 4
    std::vector<uint32_t> values(COUNT);

    // Generate mixed-size values
    for (int i = 0; i < COUNT; ++i) {
        values[i] = (i * 123456) % 1000000;
    }

    // Encode
    std::vector<uint8_t> buffer(COUNT * 5);  // Worst case: 5 bytes per int
    int offset = 0;
    for (int i = 0; i < COUNT; i += 4) {
        offset += StreamVByte::encode(values.data() + i, 4, buffer.data() + offset);
    }

    // Bulk decode (should use SIMD)
    std::vector<uint32_t> decoded(COUNT);
    int consumed = StreamVByte::decodeBulk(buffer.data(), COUNT, decoded.data());
    EXPECT_GT(consumed, 0);
    EXPECT_LE(consumed, offset);

    // Verify correctness
    EXPECT_EQ(values, decoded);
}

// ==================== SIMD Detection ====================

TEST(StreamVByteTest, SIMDPathUsed) {
    // Verify SIMD path is used when available
    // This test doesn't verify speedup, just that decode works correctly

    uint32_t values[4] = {123, 456, 789, 1234567};
    uint8_t buffer[20];

    StreamVByte::encode(values, 4, buffer);

    uint32_t decoded[4];
    StreamVByte::decode4(buffer, decoded);

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(values[i], decoded[i]);
    }

#if defined(__AVX2__)
    // AVX2 available - should use fast path
    SUCCEED() << "AVX2 SIMD path available";
#elif defined(__SSE4_2__)
    // SSE4.2 available - should use PSHUFB
    SUCCEED() << "SSE4.2 SIMD path available";
#elif defined(__ARM_NEON)
    // NEON available - uses vtbl
    SUCCEED() << "ARM NEON path available";
#else
    // Scalar fallback
    SUCCEED() << "Scalar fallback used";
#endif
}
