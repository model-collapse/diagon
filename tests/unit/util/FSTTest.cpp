// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/FST.h"

#include <gtest/gtest.h>

using namespace diagon::util;

// ==================== Basic Tests ====================

TEST(FSTTest, EmptyFST) {
    FST::Builder builder;
    auto fst = builder.finish();

    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("hello")));
}

TEST(FSTTest, SingleEntry) {
    FST::Builder builder;
    builder.add(BytesRef("hello"), 100);
    auto fst = builder.finish();

    EXPECT_EQ(100, fst->get(BytesRef("hello")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("world")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("hell")));
}

TEST(FSTTest, MultipleEntries) {
    FST::Builder builder;

    // Add in sorted order
    builder.add(BytesRef("apple"), 10);
    builder.add(BytesRef("banana"), 20);
    builder.add(BytesRef("cherry"), 30);

    auto fst = builder.finish();

    EXPECT_EQ(10, fst->get(BytesRef("apple")));
    EXPECT_EQ(20, fst->get(BytesRef("banana")));
    EXPECT_EQ(30, fst->get(BytesRef("cherry")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("durian")));
}

TEST(FSTTest, SharedPrefixes) {
    FST::Builder builder;

    builder.add(BytesRef("cat"), 1);
    builder.add(BytesRef("cats"), 2);
    builder.add(BytesRef("dog"), 3);
    builder.add(BytesRef("dogs"), 4);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(BytesRef("cat")));
    EXPECT_EQ(2, fst->get(BytesRef("cats")));
    EXPECT_EQ(3, fst->get(BytesRef("dog")));
    EXPECT_EQ(4, fst->get(BytesRef("dogs")));

    // Verify non-existent terms
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("ca")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("catsup")));
}

TEST(FSTTest, LongCommonPrefix) {
    FST::Builder builder;

    builder.add(BytesRef("internationalization"), 1);
    builder.add(BytesRef("internationalizations"), 2);
    builder.add(BytesRef("internationalizing"), 3);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(BytesRef("internationalization")));
    EXPECT_EQ(2, fst->get(BytesRef("internationalizations")));
    EXPECT_EQ(3, fst->get(BytesRef("internationalizing")));

    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("international")));
}

// ==================== Order Validation ====================

TEST(FSTTest, UnsortedInputThrows) {
    FST::Builder builder;

    builder.add(BytesRef("zebra"), 1);

    // Adding "apple" after "zebra" should throw
    EXPECT_THROW(builder.add(BytesRef("apple"), 2), std::invalid_argument);
}

TEST(FSTTest, DuplicateInputThrows) {
    FST::Builder builder;

    builder.add(BytesRef("apple"), 1);

    // Adding same term again should throw
    EXPECT_THROW(builder.add(BytesRef("apple"), 2), std::invalid_argument);
}

// ==================== Edge Cases ====================

TEST(FSTTest, EmptyString) {
    FST::Builder builder;

    builder.add(BytesRef(""), 100);
    builder.add(BytesRef("a"), 200);

    auto fst = builder.finish();

    EXPECT_EQ(100, fst->get(BytesRef("")));
    EXPECT_EQ(200, fst->get(BytesRef("a")));
}

TEST(FSTTest, SingleCharacterTerms) {
    FST::Builder builder;

    builder.add(BytesRef("a"), 1);
    builder.add(BytesRef("b"), 2);
    builder.add(BytesRef("c"), 3);

    auto fst = builder.finish();

    EXPECT_EQ(1, fst->get(BytesRef("a")));
    EXPECT_EQ(2, fst->get(BytesRef("b")));
    EXPECT_EQ(3, fst->get(BytesRef("c")));
}

TEST(FSTTest, BinaryData) {
    FST::Builder builder;

    uint8_t data1[] = {0x00, 0x01, 0x02};
    uint8_t data2[] = {0x00, 0x01, 0x03};
    uint8_t data3[] = {0xFF, 0xFE, 0xFD};

    builder.add(BytesRef(data1, 3), 10);
    builder.add(BytesRef(data2, 3), 20);
    builder.add(BytesRef(data3, 3), 30);

    auto fst = builder.finish();

    EXPECT_EQ(10, fst->get(BytesRef(data1, 3)));
    EXPECT_EQ(20, fst->get(BytesRef(data2, 3)));
    EXPECT_EQ(30, fst->get(BytesRef(data3, 3)));
}

// ==================== Longest Prefix Match ====================

TEST(FSTTest, LongestPrefixMatch_ExactMatch) {
    FST::Builder builder;
    builder.add(BytesRef("hello"), 100);
    auto fst = builder.finish();

    int prefixLen;
    FST::Output output = fst->getLongestPrefixMatch(BytesRef("hello"), prefixLen);

    EXPECT_EQ(100, output);
    EXPECT_EQ(5, prefixLen);  // "hello" length
}

TEST(FSTTest, LongestPrefixMatch_PartialMatch) {
    FST::Builder builder;
    builder.add(BytesRef("cat"), 1);
    builder.add(BytesRef("cats"), 2);
    auto fst = builder.finish();

    // Debug: Verify both terms are in FST
    EXPECT_EQ(1, fst->get(BytesRef("cat")));
    EXPECT_EQ(2, fst->get(BytesRef("cats")));

    int prefixLen;
    FST::Output output = fst->getLongestPrefixMatch(BytesRef("catsuit"), prefixLen);

    EXPECT_EQ(2, output);     // Matches "cats"
    EXPECT_EQ(4, prefixLen);  // Length of "cats"
}

TEST(FSTTest, LongestPrefixMatch_NoMatch) {
    FST::Builder builder;
    builder.add(BytesRef("apple"), 10);
    auto fst = builder.finish();

    int prefixLen;
    FST::Output output = fst->getLongestPrefixMatch(BytesRef("banana"), prefixLen);

    EXPECT_EQ(FST::NO_OUTPUT, output);
    EXPECT_EQ(0, prefixLen);
}

// ==================== Large FST ====================

TEST(FSTTest, LargeFST) {
    FST::Builder builder;

    // Add 1000 terms (zero-padded for lexicographic order)
    for (int i = 0; i < 1000; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        std::string term(buf);
        builder.add(BytesRef(term), i * 100);
    }

    auto fst = builder.finish();

    // Verify random lookups
    EXPECT_EQ(0, fst->get(BytesRef("term_0000")));
    EXPECT_EQ(50000, fst->get(BytesRef("term_0500")));
    EXPECT_EQ(99900, fst->get(BytesRef("term_0999")));

    // Verify non-existent
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("term_1000")));
    EXPECT_EQ(FST::NO_OUTPUT, fst->get(BytesRef("not_a_term")));
}
