// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/SIMDUtils.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace diagon::util::simd;

TEST(SIMDUtilsTest, PrefetchBasic) {
    // Basic test: ensure prefetch doesn't crash
    std::vector<int> data(1024);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i);
    }

    // Prefetch with different localities - should not crash
    Prefetch::read(data.data(), Prefetch::Locality::HIGH);
    Prefetch::read(data.data(), Prefetch::Locality::MEDIUM);
    Prefetch::read(data.data(), Prefetch::Locality::LOW);
    Prefetch::read(data.data(), Prefetch::Locality::NTA);

    // Verify data is unchanged
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], static_cast<int>(i));
    }
}

TEST(SIMDUtilsTest, PrefetchWrite) {
    std::vector<int> data(1024);

    // Write prefetch should not crash
    Prefetch::write(data.data(), Prefetch::Locality::HIGH);

    // Can still write to data
    data[0] = 42;
    EXPECT_EQ(data[0], 42);
}

TEST(SIMDUtilsTest, PrefetchRange) {
    std::vector<uint8_t> large_data(16 * 1024);  // 16KB
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Prefetch entire range - should not crash
    Prefetch::readRange(large_data.data(), large_data.size());

    // Verify data unchanged
    for (size_t i = 0; i < large_data.size(); ++i) {
        EXPECT_EQ(large_data[i], static_cast<uint8_t>(i & 0xFF));
    }
}

TEST(SIMDUtilsTest, PrefetchNullptr) {
    // Prefetch null pointer should not crash (it's a hint, can be ignored)
    Prefetch::read(nullptr);
    Prefetch::write(nullptr);
    Prefetch::readRange(nullptr, 0);
}

TEST(SIMDUtilsTest, AlignmentCheck) {
    alignas(32) uint8_t aligned_data[64];
    uint8_t unaligned_data[64];

    // Aligned pointer
    EXPECT_TRUE(Alignment::isAligned(aligned_data, 32));
    EXPECT_TRUE(Alignment::isAligned(aligned_data, 16));
    EXPECT_TRUE(Alignment::isAligned(aligned_data, 8));

    // Unaligned pointer (stack arrays may or may not be aligned)
    // Just check that function doesn't crash
    bool result = Alignment::isAligned(unaligned_data, 32);
    (void)result;  // Suppress unused warning
}

TEST(SIMDUtilsTest, SIMDAlignment) {
    alignas(DIAGON_SIMD_WIDTH_BYTES) uint8_t aligned_data[64];

    // Check SIMD alignment
    EXPECT_TRUE(Alignment::isSIMDAligned(aligned_data));
}

TEST(SIMDUtilsTest, AlignUp) {
    uint8_t data[128];

    // Already aligned pointer should stay the same
    uint8_t* aligned = &data[0];
    if (Alignment::isAligned(aligned, 16)) {
        EXPECT_EQ(Alignment::alignUp(aligned, 16), aligned);
    }

    // Unaligned pointer should be aligned up
    uint8_t* unaligned = &data[1];
    const void* aligned_up = Alignment::alignUp(unaligned, 16);
    EXPECT_TRUE(Alignment::isAligned(aligned_up, 16));
    EXPECT_GE(aligned_up, unaligned);
}

TEST(SIMDUtilsTest, BytesToAlign) {
    alignas(32) uint8_t aligned_data[64];

    // Already aligned
    EXPECT_EQ(Alignment::bytesToAlign(aligned_data, 32), 0);

    // Offset by 1 byte
    EXPECT_EQ(Alignment::bytesToAlign(aligned_data + 1, 32), 31);

    // Offset by 16 bytes
    EXPECT_EQ(Alignment::bytesToAlign(aligned_data + 16, 32), 16);
}

TEST(SIMDUtilsTest, CacheConstants) {
    // Just verify constants are reasonable
    EXPECT_EQ(CacheConstants::LINE_SIZE, 64);
    EXPECT_GT(CacheConstants::L1_SIZE, 0);
    EXPECT_GT(CacheConstants::L2_SIZE, CacheConstants::L1_SIZE);
    EXPECT_GT(CacheConstants::L3_SIZE, CacheConstants::L2_SIZE);
}

TEST(SIMDUtilsTest, PrefetchDistances) {
    // Verify prefetch distance constants are reasonable
    EXPECT_GT(PrefetchDistance::SEQUENTIAL_SCAN, 0);
    EXPECT_GT(PrefetchDistance::RANDOM_ACCESS, 0);
    EXPECT_GT(PrefetchDistance::COMPUTE_INTENSIVE, PrefetchDistance::SEQUENTIAL_SCAN);
    EXPECT_GT(PrefetchDistance::POSTING_LIST, 0);
}

TEST(SIMDUtilsTest, SIMDWidthConstants) {
    // Verify SIMD width constants are defined
    EXPECT_GT(DIAGON_SIMD_WIDTH_BYTES, 0);
    EXPECT_GT(DIAGON_SIMD_WIDTH_I32, 0);
    EXPECT_GT(DIAGON_SIMD_WIDTH_F32, 0);

    // Width should be consistent
    EXPECT_EQ(DIAGON_SIMD_WIDTH_BYTES, DIAGON_SIMD_WIDTH_I32 * sizeof(int32_t));
    EXPECT_EQ(DIAGON_SIMD_WIDTH_BYTES, DIAGON_SIMD_WIDTH_F32 * sizeof(float));
}
