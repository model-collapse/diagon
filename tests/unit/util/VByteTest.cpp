// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/VByte.h"

#include <gtest/gtest.h>
#include <limits>

using namespace diagon::util;

// ==================== UInt32 Tests ====================

TEST(VByteTest, EncodeDecodeUInt32_Small) {
    uint8_t buffer[10];

    // Test small values (1 byte)
    for (uint32_t val = 0; val < 128; val++) {
        int encoded = VByte::encodeUInt32(val, buffer);
        EXPECT_EQ(1, encoded);

        int decoded_bytes;
        uint32_t result = VByte::decodeUInt32(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(1, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeUInt32_Medium) {
    uint8_t buffer[10];

    // Test medium values (2 bytes)
    uint32_t test_values[] = {128, 255, 1000, 16383};
    for (uint32_t val : test_values) {
        int encoded = VByte::encodeUInt32(val, buffer);
        EXPECT_EQ(2, encoded);

        int decoded_bytes;
        uint32_t result = VByte::decodeUInt32(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(2, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeUInt32_Large) {
    uint8_t buffer[10];

    // Test large values (3+ bytes)
    uint32_t test_values[] = {16384, 100000, 1000000, 100000000};
    for (uint32_t val : test_values) {
        int encoded = VByte::encodeUInt32(val, buffer);

        int decoded_bytes;
        uint32_t result = VByte::decodeUInt32(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeUInt32_Max) {
    uint8_t buffer[10];

    uint32_t val = std::numeric_limits<uint32_t>::max();
    int encoded = VByte::encodeUInt32(val, buffer);
    EXPECT_EQ(5, encoded);  // Max uint32 needs 5 bytes

    int decoded_bytes;
    uint32_t result = VByte::decodeUInt32(buffer, &decoded_bytes);
    EXPECT_EQ(val, result);
    EXPECT_EQ(5, decoded_bytes);
}

// ==================== Int32 Tests (Zig-Zag) ====================

TEST(VByteTest, EncodeDecodeInt32_Positive) {
    uint8_t buffer[10];

    int32_t test_values[] = {0, 1, 10, 100, 1000, 10000};
    for (int32_t val : test_values) {
        int encoded = VByte::encodeInt32(val, buffer);

        int decoded_bytes;
        int32_t result = VByte::decodeInt32(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeInt32_Negative) {
    uint8_t buffer[10];

    int32_t test_values[] = {-1, -10, -100, -1000, -10000};
    for (int32_t val : test_values) {
        int encoded = VByte::encodeInt32(val, buffer);

        int decoded_bytes;
        int32_t result = VByte::decodeInt32(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeInt32_MinMax) {
    uint8_t buffer[10];

    // Test min and max int32
    int32_t test_values[] = {
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()
    };

    for (int32_t val : test_values) {
        int encoded = VByte::encodeInt32(val, buffer);

        int decoded_bytes;
        int32_t result = VByte::decodeInt32(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

// ==================== UInt64 Tests ====================

TEST(VByteTest, EncodeDecodeUInt64_Small) {
    uint8_t buffer[12];

    // Test small values
    for (uint64_t val = 0; val < 128; val++) {
        int encoded = VByte::encodeUInt64(val, buffer);
        EXPECT_EQ(1, encoded);

        int decoded_bytes;
        uint64_t result = VByte::decodeUInt64(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(1, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeUInt64_Large) {
    uint8_t buffer[12];

    // Test large values
    uint64_t test_values[] = {
        1000000000ULL,
        1000000000000ULL,
        1000000000000000ULL
    };

    for (uint64_t val : test_values) {
        int encoded = VByte::encodeUInt64(val, buffer);

        int decoded_bytes;
        uint64_t result = VByte::decodeUInt64(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeUInt64_Max) {
    uint8_t buffer[12];

    uint64_t val = std::numeric_limits<uint64_t>::max();
    int encoded = VByte::encodeUInt64(val, buffer);
    EXPECT_EQ(10, encoded);  // Max uint64 needs 10 bytes

    int decoded_bytes;
    uint64_t result = VByte::decodeUInt64(buffer, &decoded_bytes);
    EXPECT_EQ(val, result);
    EXPECT_EQ(10, decoded_bytes);
}

// ==================== Int64 Tests (Zig-Zag) ====================

TEST(VByteTest, EncodeDecodeInt64_Positive) {
    uint8_t buffer[12];

    int64_t test_values[] = {0, 1, 100, 10000, 1000000000LL};
    for (int64_t val : test_values) {
        int encoded = VByte::encodeInt64(val, buffer);

        int decoded_bytes;
        int64_t result = VByte::decodeInt64(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeInt64_Negative) {
    uint8_t buffer[12];

    int64_t test_values[] = {-1, -100, -10000, -1000000000LL};
    for (int64_t val : test_values) {
        int encoded = VByte::encodeInt64(val, buffer);

        int decoded_bytes;
        int64_t result = VByte::decodeInt64(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

TEST(VByteTest, EncodeDecodeInt64_MinMax) {
    uint8_t buffer[12];

    // Test min and max int64
    int64_t test_values[] = {
        std::numeric_limits<int64_t>::min(),
        std::numeric_limits<int64_t>::max()
    };

    for (int64_t val : test_values) {
        int encoded = VByte::encodeInt64(val, buffer);

        int decoded_bytes;
        int64_t result = VByte::decodeInt64(buffer, &decoded_bytes);
        EXPECT_EQ(val, result);
        EXPECT_EQ(encoded, decoded_bytes);
    }
}

// ==================== Encoded Size Tests ====================

TEST(VByteTest, EncodedSize_UInt32) {
    EXPECT_EQ(1, VByte::encodedSize(0U));
    EXPECT_EQ(1, VByte::encodedSize(127U));
    EXPECT_EQ(2, VByte::encodedSize(128U));
    EXPECT_EQ(2, VByte::encodedSize(16383U));
    EXPECT_EQ(3, VByte::encodedSize(16384U));
    EXPECT_EQ(5, VByte::encodedSize(std::numeric_limits<uint32_t>::max()));
}

TEST(VByteTest, EncodedSize_UInt64) {
    EXPECT_EQ(1, VByte::encodedSize(static_cast<uint64_t>(0)));
    EXPECT_EQ(1, VByte::encodedSize(static_cast<uint64_t>(127)));
    EXPECT_EQ(2, VByte::encodedSize(static_cast<uint64_t>(128)));
    EXPECT_EQ(10, VByte::encodedSize(std::numeric_limits<uint64_t>::max()));
}

// ==================== Delta Encoding Test ====================

TEST(VByteTest, DeltaEncoding) {
    uint8_t buffer[100];

    // Simulate doc ID delta encoding
    std::vector<uint32_t> docIds = {5, 12, 18, 25, 100, 200, 500};
    std::vector<uint32_t> deltas;

    uint32_t last = 0;
    for (uint32_t docId : docIds) {
        deltas.push_back(docId - last);
        last = docId;
    }

    // Encode deltas
    int offset = 0;
    for (uint32_t delta : deltas) {
        offset += VByte::encodeUInt32(delta, buffer + offset);
    }

    // Decode and reconstruct doc IDs
    std::vector<uint32_t> reconstructed;
    int read_offset = 0;
    last = 0;
    while (read_offset < offset) {
        int bytes_read;
        uint32_t delta = VByte::decodeUInt32(buffer + read_offset, &bytes_read);
        read_offset += bytes_read;
        last += delta;
        reconstructed.push_back(last);
    }

    EXPECT_EQ(docIds, reconstructed);
}
