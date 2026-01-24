// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/IOContext.h"

#include <gtest/gtest.h>

using namespace diagon::store;

class IOContextTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(IOContextTest, DefaultConstructor) {
    IOContext ctx;
    EXPECT_EQ(IOContext::Type::DEFAULT, ctx.type);
    EXPECT_FALSE(ctx.readOnce);
    EXPECT_EQ(0, ctx.mergeSize);
    EXPECT_EQ(0, ctx.flushSize);
}

TEST_F(IOContextTest, TypeConstructor) {
    IOContext ctx(IOContext::Type::MERGE);
    EXPECT_EQ(IOContext::Type::MERGE, ctx.type);
    EXPECT_FALSE(ctx.readOnce);
}

TEST_F(IOContextTest, ReadOnceType) {
    IOContext ctx(IOContext::Type::READONCE);
    EXPECT_EQ(IOContext::Type::READONCE, ctx.type);
    EXPECT_TRUE(ctx.readOnce);
}

TEST_F(IOContextTest, ForMerge) {
    IOContext ctx = IOContext::forMerge(1024 * 1024);
    EXPECT_EQ(IOContext::Type::MERGE, ctx.type);
    EXPECT_EQ(1024 * 1024, ctx.mergeSize);
}

TEST_F(IOContextTest, ForFlush) {
    IOContext ctx = IOContext::forFlush(512 * 1024);
    EXPECT_EQ(IOContext::Type::FLUSH, ctx.type);
    EXPECT_EQ(512 * 1024, ctx.flushSize);
}

TEST_F(IOContextTest, StaticConstants) {
    EXPECT_EQ(IOContext::Type::DEFAULT, IOContext::DEFAULT.type);
    EXPECT_EQ(IOContext::Type::READONCE, IOContext::READONCE.type);
    EXPECT_EQ(IOContext::Type::READ, IOContext::READ.type);
    EXPECT_EQ(IOContext::Type::MERGE, IOContext::MERGE.type);
    EXPECT_EQ(IOContext::Type::FLUSH, IOContext::FLUSH.type);

    EXPECT_TRUE(IOContext::READONCE.readOnce);
    EXPECT_FALSE(IOContext::READ.readOnce);
}
