// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace diagon::store;

class IndexInputOutputTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "diagon_test_io";
        std::filesystem::create_directories(test_dir);
        dir = FSDirectory::open(test_dir);
    }

    void TearDown() override {
        dir->close();
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
    std::unique_ptr<FSDirectory> dir;
};

TEST_F(IndexInputOutputTest, WriteAndReadByte) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeByte(0x42);
    output->writeByte(0xFF);
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0x42, input->readByte());
    EXPECT_EQ(0xFF, input->readByte());
}

TEST_F(IndexInputOutputTest, WriteAndReadBytes) {
    uint8_t data[] = {1, 2, 3, 4, 5};

    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeBytes(data, 5);
    output->close();

    uint8_t buffer[5] = {0};
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    input->readBytes(buffer, 5);

    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(data[i], buffer[i]);
    }
}

TEST_F(IndexInputOutputTest, WriteAndReadShort) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeShort(0x1234);
    output->writeShort(-1000);
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0x1234, input->readShort());
    EXPECT_EQ(-1000, input->readShort());
}

TEST_F(IndexInputOutputTest, WriteAndReadInt) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeInt(0x12345678);
    output->writeInt(-123456);
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0x12345678, input->readInt());
    EXPECT_EQ(-123456, input->readInt());
}

TEST_F(IndexInputOutputTest, WriteAndReadLong) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeLong(0x123456789ABCDEF0LL);
    output->writeLong(-1234567890123LL);
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0x123456789ABCDEF0LL, input->readLong());
    EXPECT_EQ(-1234567890123LL, input->readLong());
}

TEST_F(IndexInputOutputTest, WriteAndReadVInt) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);

    // Various sizes
    output->writeVInt(0);
    output->writeVInt(127);         // 1 byte
    output->writeVInt(128);         // 2 bytes
    output->writeVInt(16383);       // 2 bytes
    output->writeVInt(16384);       // 3 bytes
    output->writeVInt(0x7FFFFFFF);  // 5 bytes (max positive)

    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0, input->readVInt());
    EXPECT_EQ(127, input->readVInt());
    EXPECT_EQ(128, input->readVInt());
    EXPECT_EQ(16383, input->readVInt());
    EXPECT_EQ(16384, input->readVInt());
    EXPECT_EQ(0x7FFFFFFF, input->readVInt());
}

TEST_F(IndexInputOutputTest, WriteAndReadVLong) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);

    // Various sizes
    output->writeVLong(0LL);
    output->writeVLong(127LL);
    output->writeVLong(128LL);
    output->writeVLong(0x7FFFFFFFLL);
    output->writeVLong(0x7FFFFFFFFFFFFFFFLL);  // Max positive

    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0LL, input->readVLong());
    EXPECT_EQ(127LL, input->readVLong());
    EXPECT_EQ(128LL, input->readVLong());
    EXPECT_EQ(0x7FFFFFFFLL, input->readVLong());
    EXPECT_EQ(0x7FFFFFFFFFFFFFFFLL, input->readVLong());
}

TEST_F(IndexInputOutputTest, WriteAndReadString) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeString("hello");
    output->writeString("world");
    output->writeString("");
    output->writeString("longer string with spaces and 数字");
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ("hello", input->readString());
    EXPECT_EQ("world", input->readString());
    EXPECT_EQ("", input->readString());
    EXPECT_EQ("longer string with spaces and 数字", input->readString());
}

TEST_F(IndexInputOutputTest, Seek) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100; i++) {
        output->writeByte(static_cast<uint8_t>(i));
    }
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Seek to position 50
    input->seek(50);
    EXPECT_EQ(50, input->readByte());

    // Seek back to 10
    input->seek(10);
    EXPECT_EQ(10, input->readByte());

    // Seek to end
    input->seek(99);
    EXPECT_EQ(99, input->readByte());
}

TEST_F(IndexInputOutputTest, FilePointer) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0, output->getFilePointer());

    output->writeByte(0x42);
    EXPECT_EQ(1, output->getFilePointer());

    output->writeInt(0x12345678);
    EXPECT_EQ(5, output->getFilePointer());

    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0, input->getFilePointer());

    input->readByte();
    EXPECT_EQ(1, input->getFilePointer());

    input->readInt();
    EXPECT_EQ(5, input->getFilePointer());
}

TEST_F(IndexInputOutputTest, Length) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100; i++) {
        output->writeByte(static_cast<uint8_t>(i));
    }
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(100, input->length());
}

TEST_F(IndexInputOutputTest, Clone) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100; i++) {
        output->writeByte(static_cast<uint8_t>(i));
    }
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    input->seek(50);

    auto clone = input->clone();
    EXPECT_EQ(50, clone->getFilePointer());

    // Original and clone should be independent
    EXPECT_EQ(50, input->readByte());
    clone->seek(10);
    EXPECT_EQ(10, clone->readByte());

    // Original position unchanged
    EXPECT_EQ(51, input->getFilePointer());
}

TEST_F(IndexInputOutputTest, Slice) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100; i++) {
        output->writeByte(static_cast<uint8_t>(i));
    }
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Create slice from 20-29 (10 bytes)
    auto slice = input->slice("test_slice", 20, 10);
    EXPECT_EQ(10, slice->length());
    EXPECT_EQ(0, slice->getFilePointer());

    // Read from slice
    EXPECT_EQ(20, slice->readByte());  // Position 0 in slice = position 20 in file
    EXPECT_EQ(21, slice->readByte());

    // Slice position independent of parent
    EXPECT_EQ(2, slice->getFilePointer());
    EXPECT_EQ(0, input->getFilePointer());
}

TEST_F(IndexInputOutputTest, SkipBytes) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100; i++) {
        output->writeByte(static_cast<uint8_t>(i));
    }
    output->close();

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0, input->readByte());

    input->skipBytes(10);
    EXPECT_EQ(11, input->readByte());
}

TEST_F(IndexInputOutputTest, LargeFile) {
    // Test buffering with file larger than buffer size
    auto output = dir->createOutput("large.bin", IOContext::DEFAULT);
    for (int i = 0; i < 10000; i++) {
        output->writeInt(i);
    }
    output->close();

    auto input = dir->openInput("large.bin", IOContext::DEFAULT);
    for (int i = 0; i < 10000; i++) {
        EXPECT_EQ(i, input->readInt());
    }
}

TEST_F(IndexInputOutputTest, VIntEncodingSize) {
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);

    // Test that VInt uses expected number of bytes
    int64_t start = output->getFilePointer();
    output->writeVInt(127);
    EXPECT_EQ(1, output->getFilePointer() - start);

    start = output->getFilePointer();
    output->writeVInt(128);
    EXPECT_EQ(2, output->getFilePointer() - start);

    start = output->getFilePointer();
    output->writeVInt(16384);
    EXPECT_EQ(3, output->getFilePointer() - start);

    output->close();
}
