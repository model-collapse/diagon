// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/ByteBlockPool.h"
#include "diagon/util/IntBlockPool.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace diagon::util;

// ==================== ByteBlockPool Tests ====================

TEST(ByteBlockPoolTest, InitialState) {
    ByteBlockPool pool;
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.bytesUsed(), ByteBlockPool::BYTE_BLOCK_SIZE);  // One block allocated
}

TEST(ByteBlockPoolTest, AppendSingleByte) {
    ByteBlockPool pool;

    int64_t offset = pool.append(reinterpret_cast<const uint8_t*>("A"), 1);

    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool.getByte(0), 'A');
}

TEST(ByteBlockPoolTest, AppendMultipleBytes) {
    ByteBlockPool pool;

    const char* data = "Hello";
    int64_t offset = pool.append(reinterpret_cast<const uint8_t*>(data), 5);

    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.size(), 5);

    uint8_t buffer[5];
    pool.readBytes(0, buffer, 5);

    EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer), 5), "Hello");
}

TEST(ByteBlockPoolTest, AppendString) {
    ByteBlockPool pool;

    std::string str = "TestString";
    int64_t offset = pool.append(str);

    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.size(), str.length() + 1);  // +1 for null terminator

    std::string result = pool.readString(offset);
    EXPECT_EQ(result, str);
}

TEST(ByteBlockPoolTest, CrossBlockAppend) {
    ByteBlockPool pool;

    // Fill almost entire first block
    int fillSize = ByteBlockPool::BYTE_BLOCK_SIZE - 10;
    std::vector<uint8_t> fillData(fillSize, 'A');
    pool.append(fillData.data(), fillSize);

    EXPECT_EQ(pool.bytesUsed(), ByteBlockPool::BYTE_BLOCK_SIZE);  // 1 block

    // Append data that spans into second block
    const char* data = "CrossBlockData";
    int dataLen = strlen(data);
    int64_t offset = pool.append(reinterpret_cast<const uint8_t*>(data), dataLen);

    EXPECT_EQ(offset, fillSize);
    EXPECT_EQ(pool.bytesUsed(), 2 * ByteBlockPool::BYTE_BLOCK_SIZE);  // 2 blocks

    // Verify data is correctly read across blocks
    std::vector<uint8_t> buffer(dataLen);
    pool.readBytes(offset, buffer.data(), dataLen);
    EXPECT_EQ(std::string(buffer.begin(), buffer.end()), data);
}

TEST(ByteBlockPoolTest, Allocate) {
    ByteBlockPool pool;

    uint8_t* ptr = pool.allocate(100);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(pool.size(), 100);

    // Write data to allocated space
    std::memcpy(ptr, "AllocTest", 9);

    // Verify written data
    uint8_t buffer[9];
    pool.readBytes(0, buffer, 9);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer), 9), "AllocTest");
}

TEST(ByteBlockPoolTest, AllocateCrossBlock) {
    ByteBlockPool pool;

    // Fill most of first block
    int fillSize = ByteBlockPool::BYTE_BLOCK_SIZE - 50;
    pool.allocate(fillSize);

    // Request allocation larger than remaining space
    // This will trigger a new block, wasting the last 50 bytes of first block
    uint8_t* ptr = pool.allocate(100);
    EXPECT_NE(ptr, nullptr);
    // Size jumps to start of second block (fillSize rounded to BYTE_BLOCK_SIZE) + 100
    EXPECT_EQ(pool.size(), ByteBlockPool::BYTE_BLOCK_SIZE + 100);
    EXPECT_EQ(pool.bytesUsed(), 2 * ByteBlockPool::BYTE_BLOCK_SIZE);  // 2 blocks
}

TEST(ByteBlockPoolTest, Reset) {
    ByteBlockPool pool;

    // Add data
    pool.append(reinterpret_cast<const uint8_t*>("TestData"), 8);
    EXPECT_EQ(pool.size(), 8);

    // Reset
    pool.reset();
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.bytesUsed(), ByteBlockPool::BYTE_BLOCK_SIZE);  // Blocks retained

    // Can write again
    int64_t offset = pool.append(reinterpret_cast<const uint8_t*>("NewData"), 7);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.size(), 7);
}

TEST(ByteBlockPoolTest, Clear) {
    ByteBlockPool pool;

    // Add data across multiple blocks
    // First block has space from constructor, so BYTE_BLOCK_SIZE * 2 data needs 2 blocks
    std::vector<uint8_t> data(ByteBlockPool::BYTE_BLOCK_SIZE * 2);
    pool.append(data.data(), data.size());

    EXPECT_EQ(pool.bytesUsed(), 2 * ByteBlockPool::BYTE_BLOCK_SIZE);  // 2 blocks

    // Clear
    pool.clear();
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.bytesUsed(), 0);  // All blocks freed
}

TEST(ByteBlockPoolTest, MultipleStrings) {
    ByteBlockPool pool;

    // Append multiple strings
    int64_t offset1 = pool.append("First");
    int64_t offset2 = pool.append("Second");
    int64_t offset3 = pool.append("Third");

    // Verify all strings
    EXPECT_EQ(pool.readString(offset1), "First");
    EXPECT_EQ(pool.readString(offset2), "Second");
    EXPECT_EQ(pool.readString(offset3), "Third");
}

// ==================== IntBlockPool Tests ====================

TEST(IntBlockPoolTest, InitialState) {
    IntBlockPool pool;
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.bytesUsed(), IntBlockPool::INT_BLOCK_SIZE * sizeof(int));
}

TEST(IntBlockPoolTest, AppendSingleInt) {
    IntBlockPool pool;

    int offset = pool.append(42);

    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool.readInt(0), 42);
}

TEST(IntBlockPoolTest, AppendMultipleInts) {
    IntBlockPool pool;

    int offset1 = pool.append(10);
    int offset2 = pool.append(20);
    int offset3 = pool.append(30);

    EXPECT_EQ(offset1, 0);
    EXPECT_EQ(offset2, 1);
    EXPECT_EQ(offset3, 2);

    EXPECT_EQ(pool.readInt(0), 10);
    EXPECT_EQ(pool.readInt(1), 20);
    EXPECT_EQ(pool.readInt(2), 30);
}

TEST(IntBlockPoolTest, Allocate) {
    IntBlockPool pool;

    int offset = pool.allocate(5);

    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.size(), 5);

    // Write to allocated space
    pool.writeInt(0, 100);
    pool.writeInt(1, 200);
    pool.writeInt(2, 300);
    pool.writeInt(3, 400);
    pool.writeInt(4, 500);

    // Read back
    EXPECT_EQ(pool.readInt(0), 100);
    EXPECT_EQ(pool.readInt(1), 200);
    EXPECT_EQ(pool.readInt(2), 300);
    EXPECT_EQ(pool.readInt(3), 400);
    EXPECT_EQ(pool.readInt(4), 500);
}

TEST(IntBlockPoolTest, AllocateSlice) {
    IntBlockPool pool;

    int* slice = pool.allocateSlice(3);
    EXPECT_NE(slice, nullptr);
    EXPECT_EQ(pool.size(), 3);

    // Write directly to slice
    slice[0] = 111;
    slice[1] = 222;
    slice[2] = 333;

    // Read back via pool
    EXPECT_EQ(pool.readInt(0), 111);
    EXPECT_EQ(pool.readInt(1), 222);
    EXPECT_EQ(pool.readInt(2), 333);
}

TEST(IntBlockPoolTest, CrossBlockAllocation) {
    IntBlockPool pool;

    // Fill almost entire first block
    int fillSize = IntBlockPool::INT_BLOCK_SIZE - 5;
    pool.allocate(fillSize);

    EXPECT_EQ(pool.bytesUsed(), IntBlockPool::INT_BLOCK_SIZE * sizeof(int));

    // Allocate more than remaining space (10 > 5 remaining)
    // This will trigger a new block, wasting the last 5 ints of first block
    int offset = pool.allocate(10);

    // Offset is at start of second block
    EXPECT_EQ(offset, IntBlockPool::INT_BLOCK_SIZE);
    EXPECT_EQ(pool.bytesUsed(), 2 * IntBlockPool::INT_BLOCK_SIZE * sizeof(int));
}

TEST(IntBlockPoolTest, WriteAndReadAcrossBlocks) {
    IntBlockPool pool;

    // Fill first block
    int fillSize = IntBlockPool::INT_BLOCK_SIZE;
    pool.allocate(fillSize);

    // Write to end of first block
    pool.writeInt(fillSize - 1, 999);

    // Append to second block
    int offset = pool.append(888);

    // Verify both values
    EXPECT_EQ(pool.readInt(fillSize - 1), 999);
    EXPECT_EQ(pool.readInt(offset), 888);
}

TEST(IntBlockPoolTest, PostingListSimulation) {
    IntBlockPool pool;

    // Simulate storing [docID, freq] pairs for a posting list
    struct Posting {
        int docID;
        int freq;
    };

    std::vector<Posting> postings = {{0, 3}, {5, 1}, {10, 2}, {15, 4}};

    // Store postings
    int startOffset = pool.allocate(postings.size() * 2);

    for (size_t i = 0; i < postings.size(); i++) {
        pool.writeInt(startOffset + i * 2, postings[i].docID);
        pool.writeInt(startOffset + i * 2 + 1, postings[i].freq);
    }

    // Read back and verify
    for (size_t i = 0; i < postings.size(); i++) {
        int docID = pool.readInt(startOffset + i * 2);
        int freq = pool.readInt(startOffset + i * 2 + 1);

        EXPECT_EQ(docID, postings[i].docID);
        EXPECT_EQ(freq, postings[i].freq);
    }
}

TEST(IntBlockPoolTest, Reset) {
    IntBlockPool pool;

    // Add data
    pool.append(100);
    pool.append(200);
    EXPECT_EQ(pool.size(), 2);

    // Reset
    pool.reset();
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.bytesUsed(), IntBlockPool::INT_BLOCK_SIZE * sizeof(int));  // Blocks retained

    // Can write again
    int offset = pool.append(300);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(pool.readInt(0), 300);
}

TEST(IntBlockPoolTest, Clear) {
    IntBlockPool pool;

    // Add data across multiple blocks
    // Fill first block
    pool.allocate(IntBlockPool::INT_BLOCK_SIZE);
    // Add more to second block
    pool.allocate(100);

    EXPECT_EQ(pool.bytesUsed(), 2 * IntBlockPool::INT_BLOCK_SIZE * sizeof(int));

    // Clear
    pool.clear();
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.bytesUsed(), 0);  // All blocks freed
}

TEST(IntBlockPoolTest, OutOfRangeBounds) {
    IntBlockPool pool;

    pool.append(42);

    // Valid read
    EXPECT_NO_THROW(pool.readInt(0));

    // Out of range reads
    EXPECT_THROW(pool.readInt(-1), std::out_of_range);
    EXPECT_THROW(pool.readInt(1), std::out_of_range);

    // Out of range writes
    EXPECT_THROW(pool.writeInt(-1, 100), std::out_of_range);
    EXPECT_THROW(pool.writeInt(1, 100), std::out_of_range);
}

TEST(IntBlockPoolTest, AllocationValidation) {
    IntBlockPool pool;

    // Invalid allocation sizes
    EXPECT_THROW(pool.allocate(0), std::invalid_argument);
    EXPECT_THROW(pool.allocate(-1), std::invalid_argument);
    EXPECT_THROW(pool.allocate(IntBlockPool::INT_BLOCK_SIZE + 1), std::invalid_argument);

    // Valid allocation
    EXPECT_NO_THROW(pool.allocate(IntBlockPool::INT_BLOCK_SIZE));
}
