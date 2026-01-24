// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BytesRef.h"

#include <gtest/gtest.h>

using namespace diagon::util;

class BytesRefTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BytesRefTest, DefaultConstructor) {
    BytesRef ref;
    EXPECT_EQ(0, ref.length());
    EXPECT_TRUE(ref.empty());
}

TEST_F(BytesRefTest, ConstructFromCapacity) {
    BytesRef ref(10);
    EXPECT_EQ(10, ref.length());
    EXPECT_FALSE(ref.empty());
}

TEST_F(BytesRefTest, ConstructFromRawBytes) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    BytesRef ref(data, 5);

    EXPECT_EQ(5, ref.length());
    EXPECT_EQ(1, ref[0]);
    EXPECT_EQ(5, ref[4]);
}

TEST_F(BytesRefTest, ConstructFromVector) {
    std::vector<uint8_t> vec = {10, 20, 30};
    BytesRef ref(vec);

    EXPECT_EQ(3, ref.length());
    EXPECT_EQ(10, ref[0]);
    EXPECT_EQ(30, ref[2]);
}

TEST_F(BytesRefTest, ConstructFromString) {
    BytesRef ref("hello");

    EXPECT_EQ(5, ref.length());
    EXPECT_EQ('h', ref[0]);
    EXPECT_EQ('o', ref[4]);
}

TEST_F(BytesRefTest, DeepCopy) {
    std::vector<uint8_t> vec = {1, 2, 3};
    BytesRef original(vec);
    BytesRef copy = original.deepCopy();

    EXPECT_EQ(original.length(), copy.length());
    EXPECT_TRUE(original.equals(copy));

    // Modify original vector (copy should be independent)
    vec[0] = 99;
    EXPECT_EQ(1, copy[0]);  // Copy is independent
}

TEST_F(BytesRefTest, Equals) {
    std::vector<uint8_t> vec1 = {1, 2, 3};
    std::vector<uint8_t> vec2 = {1, 2, 3};
    std::vector<uint8_t> vec3 = {1, 2, 4};

    BytesRef ref1(vec1);
    BytesRef ref2(vec2);
    BytesRef ref3(vec3);

    EXPECT_TRUE(ref1.equals(ref2));
    EXPECT_FALSE(ref1.equals(ref3));
}

TEST_F(BytesRefTest, CompareTo) {
    std::vector<uint8_t> vec1 = {1, 2, 3};
    std::vector<uint8_t> vec2 = {1, 2, 4};
    std::vector<uint8_t> vec3 = {1, 2};

    BytesRef ref1(vec1);
    BytesRef ref2(vec2);
    BytesRef ref3(vec3);

    EXPECT_EQ(0, ref1.compareTo(ref1));
    EXPECT_LT(ref1.compareTo(ref2), 0);  // ref1 < ref2
    EXPECT_GT(ref2.compareTo(ref1), 0);  // ref2 > ref1
    EXPECT_GT(ref1.compareTo(ref3), 0);  // longer > shorter (same prefix)
}

TEST_F(BytesRefTest, ComparisonOperators) {
    std::vector<uint8_t> vec1 = {1, 2, 3};
    std::vector<uint8_t> vec2 = {1, 2, 3};
    std::vector<uint8_t> vec3 = {1, 2, 4};

    BytesRef ref1(vec1);
    BytesRef ref2(vec2);
    BytesRef ref3(vec3);

    EXPECT_TRUE(ref1 == ref2);
    EXPECT_FALSE(ref1 != ref2);
    EXPECT_TRUE(ref1 < ref3);
    EXPECT_TRUE(ref1 <= ref3);
    EXPECT_TRUE(ref3 > ref1);
    EXPECT_TRUE(ref3 >= ref1);
}

TEST_F(BytesRefTest, HashCode) {
    std::vector<uint8_t> vec1 = {1, 2, 3};
    std::vector<uint8_t> vec2 = {1, 2, 3};
    std::vector<uint8_t> vec3 = {1, 2, 4};

    BytesRef ref1(vec1);
    BytesRef ref2(vec2);
    BytesRef ref3(vec3);

    // Same content => same hash
    EXPECT_EQ(ref1.hashCode(), ref2.hashCode());

    // Different content => likely different hash
    EXPECT_NE(ref1.hashCode(), ref3.hashCode());
}

TEST_F(BytesRefTest, UTF8ToString) {
    BytesRef ref("hello world");
    EXPECT_EQ("hello world", ref.utf8ToString());
}

TEST_F(BytesRefTest, ToString) {
    std::vector<uint8_t> vec = {0x6c, 0x75, 0x63, 0x65, 0x6e, 0x65};  // "lucene"
    BytesRef ref(vec);

    std::string hex = ref.toString();
    EXPECT_EQ("[6c 75 63 65 6e 65]", hex);
}

TEST_F(BytesRefTest, Slice) {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 5};
    BytesRef ref(vec);

    BytesRef slice = ref.slice(1, 3);
    EXPECT_EQ(3, slice.length());
    EXPECT_EQ(2, slice[0]);
    EXPECT_EQ(4, slice[2]);
}

TEST_F(BytesRefTest, EmptyBytes) {
    BytesRef empty;
    EXPECT_EQ(0, empty.length());
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ("[]", empty.toString());
}

TEST_F(BytesRefTest, StdHashCompatibility) {
    std::vector<uint8_t> vec = {1, 2, 3};
    BytesRef ref(vec);

    // Should work with std::unordered_map
    std::hash<BytesRef> hasher;
    size_t hash = hasher(ref);
    EXPECT_EQ(ref.hashCode(), hash);
}
