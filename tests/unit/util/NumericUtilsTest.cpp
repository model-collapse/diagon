// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/NumericUtils.h"

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

using namespace diagon::util;

class NumericUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(NumericUtilsTest, FloatToSortableInt) {
    // Positive numbers
    int32_t i1 = NumericUtils::floatToSortableInt(0.0f);
    int32_t i2 = NumericUtils::floatToSortableInt(1.0f);
    int32_t i3 = NumericUtils::floatToSortableInt(100.0f);

    EXPECT_LT(i1, i2);
    EXPECT_LT(i2, i3);

    // Negative numbers
    int32_t i4 = NumericUtils::floatToSortableInt(-1.0f);
    int32_t i5 = NumericUtils::floatToSortableInt(-100.0f);

    EXPECT_LT(i5, i4);
    EXPECT_LT(i4, i1);  // negative < zero

    // Round-trip
    EXPECT_FLOAT_EQ(1.0f, NumericUtils::sortableIntToFloat(i2));
    EXPECT_FLOAT_EQ(-100.0f, NumericUtils::sortableIntToFloat(i5));
}

TEST_F(NumericUtilsTest, DoubleToSortableLong) {
    // Positive numbers
    int64_t l1 = NumericUtils::doubleToSortableLong(0.0);
    int64_t l2 = NumericUtils::doubleToSortableLong(1.0);
    int64_t l3 = NumericUtils::doubleToSortableLong(100.0);

    EXPECT_LT(l1, l2);
    EXPECT_LT(l2, l3);

    // Negative numbers
    int64_t l4 = NumericUtils::doubleToSortableLong(-1.0);
    int64_t l5 = NumericUtils::doubleToSortableLong(-100.0);

    EXPECT_LT(l5, l4);
    EXPECT_LT(l4, l1);  // negative < zero

    // Round-trip
    EXPECT_DOUBLE_EQ(1.0, NumericUtils::sortableLongToDouble(l2));
    EXPECT_DOUBLE_EQ(-100.0, NumericUtils::sortableLongToDouble(l5));
}

TEST_F(NumericUtilsTest, FloatSortOrderWithNaN) {
    float positiveInf = std::numeric_limits<float>::infinity();
    float negativeInf = -std::numeric_limits<float>::infinity();
    float nan = std::numeric_limits<float>::quiet_NaN();

    int32_t iPosInf = NumericUtils::floatToSortableInt(positiveInf);
    int32_t iNegInf = NumericUtils::floatToSortableInt(negativeInf);
    int32_t iNaN = NumericUtils::floatToSortableInt(nan);
    int32_t iZero = NumericUtils::floatToSortableInt(0.0f);

    // NaN > +Inf > 0 > -Inf
    EXPECT_LT(iNegInf, iZero);
    EXPECT_LT(iZero, iPosInf);
    EXPECT_LT(iPosInf, iNaN);
}

TEST_F(NumericUtilsTest, IntToBytesBE) {
    uint8_t bytes[4];
    NumericUtils::intToBytesBE(0x12345678, bytes);

    EXPECT_EQ(0x12, bytes[0]);
    EXPECT_EQ(0x34, bytes[1]);
    EXPECT_EQ(0x56, bytes[2]);
    EXPECT_EQ(0x78, bytes[3]);
}

TEST_F(NumericUtilsTest, BytesToIntBE) {
    uint8_t bytes[] = {0x12, 0x34, 0x56, 0x78};
    int32_t value = NumericUtils::bytesToIntBE(bytes);

    EXPECT_EQ(0x12345678, value);
}

TEST_F(NumericUtilsTest, LongToBytesBE) {
    uint8_t bytes[8];
    NumericUtils::longToBytesBE(0x123456789ABCDEF0LL, bytes);

    EXPECT_EQ(0x12, bytes[0]);
    EXPECT_EQ(0x34, bytes[1]);
    EXPECT_EQ(0x56, bytes[2]);
    EXPECT_EQ(0x78, bytes[3]);
    EXPECT_EQ(0x9A, bytes[4]);
    EXPECT_EQ(0xBC, bytes[5]);
    EXPECT_EQ(0xDE, bytes[6]);
    EXPECT_EQ(0xF0, bytes[7]);
}

TEST_F(NumericUtilsTest, BytesToLongBE) {
    uint8_t bytes[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    int64_t value = NumericUtils::bytesToLongBE(bytes);

    EXPECT_EQ(0x123456789ABCDEF0LL, value);
}

TEST_F(NumericUtilsTest, IntRoundTrip) {
    int32_t original = 0x12345678;
    uint8_t bytes[4];

    NumericUtils::intToBytesBE(original, bytes);
    int32_t restored = NumericUtils::bytesToIntBE(bytes);

    EXPECT_EQ(original, restored);
}

TEST_F(NumericUtilsTest, LongRoundTrip) {
    int64_t original = 0x123456789ABCDEF0LL;
    uint8_t bytes[8];

    NumericUtils::longToBytesBE(original, bytes);
    int64_t restored = NumericUtils::bytesToLongBE(bytes);

    EXPECT_EQ(original, restored);
}

TEST_F(NumericUtilsTest, FloatToBytesBE) {
    uint8_t bytes[4];
    NumericUtils::floatToBytesBE(3.14159f, bytes);

    float restored = NumericUtils::bytesToFloatBE(bytes);
    EXPECT_FLOAT_EQ(3.14159f, restored);
}

TEST_F(NumericUtilsTest, DoubleToBytesBE) {
    uint8_t bytes[8];
    NumericUtils::doubleToBytesBE(3.14159265359, bytes);

    double restored = NumericUtils::bytesToDoubleBE(bytes);
    EXPECT_DOUBLE_EQ(3.14159265359, restored);
}

TEST_F(NumericUtilsTest, SortableFloatBytes) {
    // Test that sortable format maintains proper sort order
    int32_t s1 = NumericUtils::floatToSortableInt(-100.0f);
    int32_t s2 = NumericUtils::floatToSortableInt(0.0f);
    int32_t s3 = NumericUtils::floatToSortableInt(100.0f);

    // Sortable ints should maintain order
    EXPECT_LT(s1, s2);  // -100 < 0
    EXPECT_LT(s2, s3);  // 0 < 100
}

TEST_F(NumericUtilsTest, SortableDoubleBytes) {
    // Test that sortable format maintains proper sort order
    int64_t s1 = NumericUtils::doubleToSortableLong(-100.0);
    int64_t s2 = NumericUtils::doubleToSortableLong(0.0);
    int64_t s3 = NumericUtils::doubleToSortableLong(100.0);

    // Sortable longs should maintain order
    EXPECT_LT(s1, s2);  // -100 < 0
    EXPECT_LT(s2, s3);  // 0 < 100
}

TEST_F(NumericUtilsTest, NegativeNumbers) {
    // Test negative int
    int32_t negInt = -12345;
    uint8_t bytes[4];
    NumericUtils::intToBytesBE(negInt, bytes);
    int32_t restored = NumericUtils::bytesToIntBE(bytes);
    EXPECT_EQ(negInt, restored);

    // Test negative long
    int64_t negLong = -1234567890123LL;
    uint8_t bytes2[8];
    NumericUtils::longToBytesBE(negLong, bytes2);
    int64_t restored2 = NumericUtils::bytesToLongBE(bytes2);
    EXPECT_EQ(negLong, restored2);
}

TEST_F(NumericUtilsTest, EdgeCases) {
    // Min/max int
    int32_t minInt = std::numeric_limits<int32_t>::min();
    int32_t maxInt = std::numeric_limits<int32_t>::max();
    uint8_t bytes[4];

    NumericUtils::intToBytesBE(minInt, bytes);
    EXPECT_EQ(minInt, NumericUtils::bytesToIntBE(bytes));

    NumericUtils::intToBytesBE(maxInt, bytes);
    EXPECT_EQ(maxInt, NumericUtils::bytesToIntBE(bytes));

    // Min/max long
    int64_t minLong = std::numeric_limits<int64_t>::min();
    int64_t maxLong = std::numeric_limits<int64_t>::max();
    uint8_t bytes2[8];

    NumericUtils::longToBytesBE(minLong, bytes2);
    EXPECT_EQ(minLong, NumericUtils::bytesToLongBE(bytes2));

    NumericUtils::longToBytesBE(maxLong, bytes2);
    EXPECT_EQ(maxLong, NumericUtils::bytesToLongBE(bytes2));
}
