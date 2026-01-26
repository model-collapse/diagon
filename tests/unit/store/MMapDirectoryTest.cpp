// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/MMapDirectory.h"

#include "diagon/store/FSDirectory.h"
#include "diagon/util/Exceptions.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace diagon;
using namespace diagon::store;

class MMapDirectoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "diagon_test_mmapdir";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override { std::filesystem::remove_all(test_dir); }

    // Helper: Create a test file with FSDirectory
    void createTestFile(const std::string& filename, const std::vector<uint8_t>& data) {
        auto dir = FSDirectory::open(test_dir);
        auto output = dir->createOutput(filename, IOContext::DEFAULT);
        output->writeBytes(data.data(), data.size());
        output->close();
    }

    // Helper: Create a test file with specific pattern
    void createTestFileWithPattern(const std::string& filename, size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i & 0xFF);
        }
        createTestFile(filename, data);
    }

    std::filesystem::path test_dir;
};

// ==================== Construction and Opening ====================

TEST_F(MMapDirectoryTest, Open) {
    auto dir = MMapDirectory::open(test_dir);
    ASSERT_NE(nullptr, dir);
    EXPECT_FALSE(dir->isClosed());
}

TEST_F(MMapDirectoryTest, OpenWithCustomChunkPower) {
    auto dir = MMapDirectory::open(test_dir, 24);  // 16MB chunks
    ASSERT_NE(nullptr, dir);
    EXPECT_EQ(24, dir->getChunkPower());
    EXPECT_EQ(16 * 1024 * 1024, dir->getChunkSize());
}

TEST_F(MMapDirectoryTest, OpenWithInvalidChunkPower) {
    // Too small (< 20)
    EXPECT_THROW(MMapDirectory::open(test_dir, 15), std::invalid_argument);

    // Too large (> 40)
    EXPECT_THROW(MMapDirectory::open(test_dir, 45), std::invalid_argument);
}

TEST_F(MMapDirectoryTest, DefaultChunkPower) {
    auto dir = MMapDirectory::open(test_dir);

    if (sizeof(void*) == 8) {
        // 64-bit: expect 16GB chunks (2^34)
        EXPECT_EQ(MMapDirectory::DEFAULT_CHUNK_POWER_64, dir->getChunkPower());
    } else {
        // 32-bit: expect 256MB chunks (2^28)
        EXPECT_EQ(MMapDirectory::DEFAULT_CHUNK_POWER_32, dir->getChunkPower());
    }
}

TEST_F(MMapDirectoryTest, ToString) {
    auto dir = MMapDirectory::open(test_dir);
    std::string str = dir->toString();

    EXPECT_NE(std::string::npos, str.find("MMapDirectory"));
    EXPECT_NE(std::string::npos, str.find(test_dir.string()));
    EXPECT_NE(std::string::npos, str.find("chunk="));
}

// ==================== Basic Reading ====================

TEST_F(MMapDirectoryTest, OpenInput) {
    // Create test file
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    createTestFile("test.bin", data);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    ASSERT_NE(nullptr, input);
    EXPECT_EQ(5, input->length());
    EXPECT_EQ(0, input->getFilePointer());
}

TEST_F(MMapDirectoryTest, OpenInputFileNotFound) {
    auto dir = MMapDirectory::open(test_dir);
    EXPECT_THROW(dir->openInput("nonexistent.bin", IOContext::DEFAULT), FileNotFoundException);
}

TEST_F(MMapDirectoryTest, ReadByte) {
    std::vector<uint8_t> data = {42, 99, 123, 200, 255};
    createTestFile("test.bin", data);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    EXPECT_EQ(42, input->readByte());
    EXPECT_EQ(1, input->getFilePointer());

    EXPECT_EQ(99, input->readByte());
    EXPECT_EQ(2, input->getFilePointer());

    EXPECT_EQ(123, input->readByte());
    EXPECT_EQ(200, input->readByte());
    EXPECT_EQ(255, input->readByte());
    EXPECT_EQ(5, input->getFilePointer());
}

TEST_F(MMapDirectoryTest, ReadBytes) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    createTestFile("test.bin", data);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    uint8_t buffer[5];
    input->readBytes(buffer, 5);

    EXPECT_EQ(1, buffer[0]);
    EXPECT_EQ(2, buffer[1]);
    EXPECT_EQ(3, buffer[2]);
    EXPECT_EQ(4, buffer[3]);
    EXPECT_EQ(5, buffer[4]);
    EXPECT_EQ(5, input->getFilePointer());
}

TEST_F(MMapDirectoryTest, ReadMultiByte) {
    std::vector<uint8_t> data = {
        0x00, 0x2A,  // short: 42
        0x00, 0x00, 0x01, 0x00,  // int: 256
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00  // long: 256
    };
    createTestFile("test.bin", data);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    EXPECT_EQ(42, input->readShort());
    EXPECT_EQ(256, input->readInt());
    EXPECT_EQ(256, input->readLong());
}

TEST_F(MMapDirectoryTest, ReadPastEOF) {
    std::vector<uint8_t> data = {1, 2, 3};
    createTestFile("test.bin", data);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    input->readByte();
    input->readByte();
    input->readByte();

    // Should throw EOFException
    EXPECT_THROW(input->readByte(), EOFException);
}

TEST_F(MMapDirectoryTest, ReadBytesPastEOF) {
    std::vector<uint8_t> data = {1, 2, 3};
    createTestFile("test.bin", data);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    uint8_t buffer[5];
    EXPECT_THROW(input->readBytes(buffer, 5), EOFException);
}

// ==================== Positioning ====================

TEST_F(MMapDirectoryTest, Seek) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Seek to position 50
    input->seek(50);
    EXPECT_EQ(50, input->getFilePointer());
    EXPECT_EQ(50, input->readByte());

    // Seek backward
    input->seek(10);
    EXPECT_EQ(10, input->getFilePointer());
    EXPECT_EQ(10, input->readByte());

    // Seek to end
    input->seek(100);
    EXPECT_EQ(100, input->getFilePointer());
}

TEST_F(MMapDirectoryTest, SeekNegative) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    EXPECT_THROW(input->seek(-1), IOException);
}

TEST_F(MMapDirectoryTest, SeekBeyondEOF) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    EXPECT_THROW(input->seek(101), IOException);
}

// ==================== Clone ====================

TEST_F(MMapDirectoryTest, Clone) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Read some data
    input->readByte();
    input->readByte();
    EXPECT_EQ(2, input->getFilePointer());

    // Clone
    auto cloned = input->clone();
    ASSERT_NE(nullptr, cloned);

    // Clone should have independent position (reset to 0)
    EXPECT_EQ(0, cloned->getFilePointer());
    EXPECT_EQ(2, input->getFilePointer());  // Original unchanged

    // Clone should read the same data
    EXPECT_EQ(0, cloned->readByte());
    EXPECT_EQ(1, cloned->readByte());
    EXPECT_EQ(2, cloned->getFilePointer());

    // Original should still be at position 2
    EXPECT_EQ(2, input->getFilePointer());
}

TEST_F(MMapDirectoryTest, CloneIndependentPosition) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    auto clone1 = input->clone();
    auto clone2 = input->clone();

    // Each clone has independent position
    input->seek(10);
    clone1->seek(20);
    clone2->seek(30);

    EXPECT_EQ(10, input->getFilePointer());
    EXPECT_EQ(20, clone1->getFilePointer());
    EXPECT_EQ(30, clone2->getFilePointer());

    // Reading from each doesn't affect others
    EXPECT_EQ(10, input->readByte());
    EXPECT_EQ(20, clone1->readByte());
    EXPECT_EQ(30, clone2->readByte());

    EXPECT_EQ(11, input->getFilePointer());
    EXPECT_EQ(21, clone1->getFilePointer());
    EXPECT_EQ(31, clone2->getFilePointer());
}

// ==================== Slice ====================

TEST_F(MMapDirectoryTest, Slice) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Create slice from offset 10, length 20
    auto sliced = input->slice("test_slice", 10, 20);
    ASSERT_NE(nullptr, sliced);

    EXPECT_EQ(0, sliced->getFilePointer());
    EXPECT_EQ(20, sliced->length());

    // Slice should read data starting from offset 10
    EXPECT_EQ(10, sliced->readByte());
    EXPECT_EQ(11, sliced->readByte());
    EXPECT_EQ(2, sliced->getFilePointer());
}

TEST_F(MMapDirectoryTest, SliceBounds) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Slice to end of file
    auto sliced = input->slice("test_slice", 50, 50);
    EXPECT_EQ(50, sliced->length());
    sliced->seek(49);
    EXPECT_EQ(99, sliced->readByte());  // Last byte
    EXPECT_THROW(sliced->readByte(), EOFException);
}

TEST_F(MMapDirectoryTest, SliceInvalidBounds) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // Invalid slices
    EXPECT_THROW(input->slice("bad_offset", -1, 10), IOException);
    EXPECT_THROW(input->slice("bad_length", 0, -1), IOException);
    EXPECT_THROW(input->slice("beyond_end", 90, 20), IOException);  // 90+20 > 100
    EXPECT_THROW(input->slice("way_beyond", 200, 10), IOException);
}

TEST_F(MMapDirectoryTest, SliceOfSlice) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);

    // First slice: [20, 60)
    auto slice1 = input->slice("slice1", 20, 40);
    EXPECT_EQ(40, slice1->length());

    // Second slice: [10, 30) relative to slice1 = [30, 50) absolute
    auto slice2 = slice1->slice("slice2", 10, 20);
    EXPECT_EQ(20, slice2->length());

    // Should read from absolute position 30
    EXPECT_EQ(30, slice2->readByte());
    EXPECT_EQ(31, slice2->readByte());
}

// ==================== Edge Cases ====================

TEST_F(MMapDirectoryTest, EmptyFile) {
    createTestFile("empty.bin", {});

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("empty.bin", IOContext::DEFAULT);

    EXPECT_EQ(0, input->length());
    EXPECT_EQ(0, input->getFilePointer());
    EXPECT_THROW(input->readByte(), EOFException);
}

TEST_F(MMapDirectoryTest, SingleByteFile) {
    createTestFile("single.bin", {42});

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("single.bin", IOContext::DEFAULT);

    EXPECT_EQ(1, input->length());
    EXPECT_EQ(42, input->readByte());
    EXPECT_THROW(input->readByte(), EOFException);
}

TEST_F(MMapDirectoryTest, ChunkBoundaryRead) {
    // Create file that spans multiple chunks (use small chunk size for testing)
    auto dir = MMapDirectory::open(test_dir, 20);  // 1MB chunks

    // Create 2MB file (spans 2 chunks)
    size_t file_size = 2 * 1024 * 1024;
    createTestFileWithPattern("large.bin", file_size);

    auto input = dir->openInput("large.bin", IOContext::DEFAULT);

    // Seek near chunk boundary (1MB - 1 byte)
    size_t chunk_boundary = 1024 * 1024;
    input->seek(chunk_boundary - 1);

    // Read across chunk boundary
    uint8_t buffer[3];
    input->readBytes(buffer, 3);

    // Verify data
    EXPECT_EQ((chunk_boundary - 1) & 0xFF, buffer[0]);
    EXPECT_EQ(chunk_boundary & 0xFF, buffer[1]);
    EXPECT_EQ((chunk_boundary + 1) & 0xFF, buffer[2]);
}

TEST_F(MMapDirectoryTest, ExactlyOneChunk) {
    // Create file exactly 1MB (one chunk with chunk_power=20)
    auto dir = MMapDirectory::open(test_dir, 20);  // 1MB chunks

    size_t file_size = 1024 * 1024;  // Exactly 1MB
    createTestFileWithPattern("exact_chunk.bin", file_size);

    auto input = dir->openInput("exact_chunk.bin", IOContext::DEFAULT);

    EXPECT_EQ(file_size, static_cast<size_t>(input->length()));

    // Seek to last byte
    input->seek(file_size - 1);
    uint8_t last_byte = input->readByte();
    EXPECT_EQ((file_size - 1) & 0xFF, last_byte);
}

// ==================== Lifecycle ====================

// Note: MMapIndexInput uses RAII for resource management.
// No explicit close() method is needed - cleanup happens automatically
// when the IndexInput is destroyed (via destructor).

TEST_F(MMapDirectoryTest, AutomaticCleanup) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);

    {
        auto input = dir->openInput("test.bin", IOContext::DEFAULT);
        EXPECT_EQ(0, input->readByte());
        // input is destroyed here, cleanup happens automatically
    }

    // Can open again after previous input is destroyed
    auto input2 = dir->openInput("test.bin", IOContext::DEFAULT);
    EXPECT_EQ(0, input2->readByte());
}

TEST_F(MMapDirectoryTest, CleanupWithSharedReferences) {
    createTestFileWithPattern("test.bin", 100);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    auto cloned = input->clone();

    // Destroy original
    input.reset();

    // Clone should still work (shared memory mapping via shared_ptr)
    EXPECT_EQ(0, cloned->readByte());
    EXPECT_EQ(1, cloned->readByte());
}

// ==================== Configuration ====================

TEST_F(MMapDirectoryTest, PreloadConfiguration) {
    auto dir = MMapDirectory::open(test_dir);

    EXPECT_FALSE(dir->isPreload());

    dir->setPreload(true);
    EXPECT_TRUE(dir->isPreload());

    dir->setPreload(false);
    EXPECT_FALSE(dir->isPreload());
}
