// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BitSet.h"

#include <gtest/gtest.h>

using namespace diagon::util;

class BitSetTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BitSetTest, Constructor) {
    BitSet bs(100);
    EXPECT_EQ(100, bs.length());
    EXPECT_EQ(0, bs.cardinality());
}

TEST_F(BitSetTest, SetAndGet) {
    BitSet bs(100);

    bs.set(5);
    EXPECT_TRUE(bs.get(5));
    EXPECT_FALSE(bs.get(4));
    EXPECT_FALSE(bs.get(6));

    bs.set(99);
    EXPECT_TRUE(bs.get(99));
}

TEST_F(BitSetTest, GetAndSet) {
    BitSet bs(100);

    bool previous = bs.getAndSet(10);
    EXPECT_FALSE(previous);
    EXPECT_TRUE(bs.get(10));

    previous = bs.getAndSet(10);
    EXPECT_TRUE(previous);
    EXPECT_TRUE(bs.get(10));
}

TEST_F(BitSetTest, Clear) {
    BitSet bs(100);

    bs.set(5);
    bs.set(10);
    bs.set(15);
    EXPECT_EQ(3, bs.cardinality());

    bs.clear(10);
    EXPECT_FALSE(bs.get(10));
    EXPECT_EQ(2, bs.cardinality());
}

TEST_F(BitSetTest, ClearRange) {
    BitSet bs(100);

    // Set bits 10-19
    for (size_t i = 10; i < 20; i++) {
        bs.set(i);
    }
    EXPECT_EQ(10, bs.cardinality());

    // Clear 12-17
    bs.clear(12, 17);
    EXPECT_TRUE(bs.get(10));
    EXPECT_TRUE(bs.get(11));
    EXPECT_FALSE(bs.get(12));
    EXPECT_FALSE(bs.get(16));
    EXPECT_TRUE(bs.get(17));
    EXPECT_EQ(5, bs.cardinality());
}

TEST_F(BitSetTest, ClearAll) {
    BitSet bs(100);

    bs.set(5);
    bs.set(10);
    bs.set(15);
    EXPECT_EQ(3, bs.cardinality());

    bs.clear();
    EXPECT_EQ(0, bs.cardinality());
    EXPECT_FALSE(bs.get(5));
    EXPECT_FALSE(bs.get(10));
    EXPECT_FALSE(bs.get(15));
}

TEST_F(BitSetTest, Cardinality) {
    BitSet bs(100);

    EXPECT_EQ(0, bs.cardinality());

    bs.set(0);
    bs.set(1);
    bs.set(99);
    EXPECT_EQ(3, bs.cardinality());
}

TEST_F(BitSetTest, NextSetBit) {
    BitSet bs(200);

    bs.set(10);
    bs.set(20);
    bs.set(150);

    EXPECT_EQ(10, bs.nextSetBit(0));
    EXPECT_EQ(10, bs.nextSetBit(5));
    EXPECT_EQ(10, bs.nextSetBit(10));
    EXPECT_EQ(20, bs.nextSetBit(11));
    EXPECT_EQ(150, bs.nextSetBit(21));
    EXPECT_EQ(BitSet::NO_MORE_BITS, bs.nextSetBit(151));
}

TEST_F(BitSetTest, PrevSetBit) {
    BitSet bs(200);

    bs.set(10);
    bs.set(20);
    bs.set(150);

    EXPECT_EQ(150, bs.prevSetBit(199));
    EXPECT_EQ(150, bs.prevSetBit(150));
    EXPECT_EQ(20, bs.prevSetBit(149));
    EXPECT_EQ(20, bs.prevSetBit(20));
    EXPECT_EQ(10, bs.prevSetBit(19));
    EXPECT_EQ(BitSet::NO_MORE_BITS, bs.prevSetBit(9));
}

TEST_F(BitSetTest, OR) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);

    bs2.set(20);
    bs2.set(30);

    bs1.OR(bs2);

    EXPECT_TRUE(bs1.get(10));
    EXPECT_TRUE(bs1.get(20));
    EXPECT_TRUE(bs1.get(30));
    EXPECT_EQ(3, bs1.cardinality());
}

TEST_F(BitSetTest, AND) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);
    bs1.set(30);

    bs2.set(20);
    bs2.set(30);
    bs2.set(40);

    bs1.AND(bs2);

    EXPECT_FALSE(bs1.get(10));
    EXPECT_TRUE(bs1.get(20));
    EXPECT_TRUE(bs1.get(30));
    EXPECT_FALSE(bs1.get(40));
    EXPECT_EQ(2, bs1.cardinality());
}

TEST_F(BitSetTest, ANDNOT) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);
    bs1.set(30);

    bs2.set(20);
    bs2.set(40);

    bs1.ANDNOT(bs2);

    EXPECT_TRUE(bs1.get(10));
    EXPECT_FALSE(bs1.get(20));
    EXPECT_TRUE(bs1.get(30));
    EXPECT_FALSE(bs1.get(40));
    EXPECT_EQ(2, bs1.cardinality());
}

TEST_F(BitSetTest, XOR) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);

    bs2.set(20);
    bs2.set(30);

    bs1.XOR(bs2);

    EXPECT_TRUE(bs1.get(10));
    EXPECT_FALSE(bs1.get(20));  // XOR: same values cancel
    EXPECT_TRUE(bs1.get(30));
    EXPECT_EQ(2, bs1.cardinality());
}

TEST_F(BitSetTest, Intersects) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);

    bs2.set(30);
    EXPECT_FALSE(bs1.intersects(bs2));

    bs2.set(20);
    EXPECT_TRUE(bs1.intersects(bs2));
}

TEST_F(BitSetTest, IntersectionCount) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);
    bs1.set(30);

    bs2.set(20);
    bs2.set(30);
    bs2.set(40);

    EXPECT_EQ(2, BitSet::intersectionCount(bs1, bs2));
}

TEST_F(BitSetTest, UnionCount) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);
    bs1.set(30);

    bs2.set(20);
    bs2.set(30);
    bs2.set(40);

    EXPECT_EQ(4, BitSet::unionCount(bs1, bs2));
}

TEST_F(BitSetTest, AndNotCount) {
    BitSet bs1(100);
    BitSet bs2(100);

    bs1.set(10);
    bs1.set(20);
    bs1.set(30);

    bs2.set(20);
    bs2.set(40);

    EXPECT_EQ(2, BitSet::andNotCount(bs1, bs2));  // 10, 30
}

TEST_F(BitSetTest, Clone) {
    BitSet bs1(100);
    bs1.set(10);
    bs1.set(20);
    bs1.set(30);

    auto bs2 = bs1.clone();
    EXPECT_EQ(bs1.cardinality(), bs2->cardinality());
    EXPECT_TRUE(bs2->get(10));
    EXPECT_TRUE(bs2->get(20));
    EXPECT_TRUE(bs2->get(30));
}

TEST_F(BitSetTest, Bits2Words) {
    EXPECT_EQ(0, BitSet::bits2words(0));
    EXPECT_EQ(1, BitSet::bits2words(1));
    EXPECT_EQ(1, BitSet::bits2words(64));
    EXPECT_EQ(2, BitSet::bits2words(65));
    EXPECT_EQ(2, BitSet::bits2words(128));
    EXPECT_EQ(3, BitSet::bits2words(129));
}

TEST_F(BitSetTest, LargeBitSet) {
    // Test with large bit set (multiple words)
    BitSet bs(10000);

    bs.set(0);
    bs.set(9999);
    bs.set(5000);

    EXPECT_TRUE(bs.get(0));
    EXPECT_TRUE(bs.get(9999));
    EXPECT_TRUE(bs.get(5000));
    EXPECT_EQ(3, bs.cardinality());

    EXPECT_EQ(0, bs.nextSetBit(0));
    EXPECT_EQ(5000, bs.nextSetBit(1));
    EXPECT_EQ(9999, bs.nextSetBit(5001));
}
