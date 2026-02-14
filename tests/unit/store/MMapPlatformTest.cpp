// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/FSDirectory.h"
#include "diagon/store/IOContext.h"
#include "diagon/store/MMapDirectory.h"
#include "diagon/util/Exceptions.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

using namespace diagon;
using namespace diagon::store;

class MMapPlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "diagon_test_mmap_platform";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override { std::filesystem::remove_all(test_dir); }

    // Helper: Create a test file
    void createTestFile(const std::string& filename, size_t size) {
        auto dir = FSDirectory::open(test_dir);
        auto output = dir->createOutput(filename, IOContext::DEFAULT);

        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i & 0xFF);
        }

        output->writeBytes(data.data(), data.size());
        output->close();
    }

    std::filesystem::path test_dir;
};

// ==================== IOContext ReadAdvice Mapping ====================

TEST_F(MMapPlatformTest, IOContextReadAdviceMapping) {
    // Test that IOContext types map to correct ReadAdvice

    IOContext default_ctx(IOContext::Type::DEFAULT);
    EXPECT_EQ(IOContext::ReadAdvice::NORMAL, default_ctx.getReadAdvice());

    IOContext merge_ctx(IOContext::Type::MERGE);
    EXPECT_EQ(IOContext::ReadAdvice::SEQUENTIAL, merge_ctx.getReadAdvice());

    IOContext flush_ctx(IOContext::Type::FLUSH);
    EXPECT_EQ(IOContext::ReadAdvice::SEQUENTIAL, flush_ctx.getReadAdvice());

    IOContext readonce_ctx(IOContext::Type::READONCE);
    EXPECT_EQ(IOContext::ReadAdvice::SEQUENTIAL, readonce_ctx.getReadAdvice());

    IOContext read_ctx(IOContext::Type::READ);
    EXPECT_EQ(IOContext::ReadAdvice::RANDOM, read_ctx.getReadAdvice());
}

// ==================== Platform-Specific Opening ====================

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)

TEST_F(MMapPlatformTest, PosixMMapWithDifferentContexts) {
    // Create a test file
    createTestFile("test.bin", 1024 * 1024);  // 1MB file

    auto dir = MMapDirectory::open(test_dir);

    // Open with SEQUENTIAL context
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::MERGE));
        ASSERT_NE(nullptr, input);
        EXPECT_EQ(0, input->readByte());
    }

    // Open with RANDOM context
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::READ));
        ASSERT_NE(nullptr, input);
        EXPECT_EQ(0, input->readByte());
    }

    // Open with NORMAL context
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::DEFAULT));
        ASSERT_NE(nullptr, input);
        EXPECT_EQ(0, input->readByte());
    }
}

TEST_F(MMapPlatformTest, PosixMMapPreload) {
    // Create a large test file
    size_t file_size = 4 * 1024 * 1024;  // 4MB
    createTestFile("large.bin", file_size);

    auto dir = MMapDirectory::open(test_dir);

    // Enable preload
    dir->setPreload(true);
    EXPECT_TRUE(dir->isPreload());

    // Open file - should preload pages
    auto input = dir->openInput("large.bin", IOContext::DEFAULT);
    ASSERT_NE(nullptr, input);

    // File should be mapped and accessible
    EXPECT_EQ(file_size, static_cast<size_t>(input->length()));
    EXPECT_EQ(0, input->readByte());

    // Seek to various positions (should be fast, pages already loaded)
    input->seek(1024 * 1024);
    EXPECT_EQ((1024 * 1024) & 0xFF, input->readByte());

    input->seek(2 * 1024 * 1024);
    EXPECT_EQ((2 * 1024 * 1024) & 0xFF, input->readByte());
}

TEST_F(MMapPlatformTest, PosixMMapNoPreload) {
    // Create test file
    createTestFile("test.bin", 1024 * 1024);

    auto dir = MMapDirectory::open(test_dir);

    // Disable preload (default)
    dir->setPreload(false);
    EXPECT_FALSE(dir->isPreload());

    // Open file - pages loaded on demand
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    ASSERT_NE(nullptr, input);
    EXPECT_EQ(0, input->readByte());
}

TEST_F(MMapPlatformTest, MadviseDoesNotCauseCrash) {
    // Test that madvise doesn't cause crashes even with various patterns
    createTestFile("test.bin", 10 * 1024 * 1024);  // 10MB

    auto dir = MMapDirectory::open(test_dir);

    // Sequential access hint
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::MERGE));
        for (int i = 0; i < 1000; ++i) {
            input->readByte();
        }
    }

    // Random access hint
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::READ));
        input->seek(1024 * 1024);
        input->readByte();
        input->seek(5 * 1024 * 1024);
        input->readByte();
    }
}

#endif  // POSIX platforms

// ==================== Windows Platform Tests ====================

#if defined(_WIN32)

TEST_F(MMapPlatformTest, WindowsMMapBasicOperation) {
    // Windows support now implemented
    createTestFile("test.bin", 1024);

    auto dir = MMapDirectory::open(test_dir);

    // Should successfully open with Windows mmap
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    ASSERT_NE(nullptr, input);
    EXPECT_EQ(1024, input->length());
    EXPECT_EQ(0, input->readByte());
}

TEST_F(MMapPlatformTest, WindowsMMapWithDifferentContexts) {
    // Test Windows implementation with different IOContext types
    createTestFile("test.bin", 1024 * 1024);  // 1MB file

    auto dir = MMapDirectory::open(test_dir);

    // Open with SEQUENTIAL context
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::MERGE));
        ASSERT_NE(nullptr, input);
        EXPECT_EQ(0, input->readByte());
    }

    // Open with RANDOM context
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::READ));
        ASSERT_NE(nullptr, input);
        EXPECT_EQ(0, input->readByte());
    }

    // Open with NORMAL context
    {
        auto input = dir->openInput("test.bin", IOContext(IOContext::Type::DEFAULT));
        ASSERT_NE(nullptr, input);
        EXPECT_EQ(0, input->readByte());
    }
}

TEST_F(MMapPlatformTest, WindowsMMapPreload) {
    // Test Windows preload functionality
    size_t file_size = 4 * 1024 * 1024;  // 4MB
    createTestFile("large.bin", file_size);

    auto dir = MMapDirectory::open(test_dir);

    // Enable preload
    dir->setPreload(true);
    EXPECT_TRUE(dir->isPreload());

    // Open file - should preload pages
    auto input = dir->openInput("large.bin", IOContext::DEFAULT);
    ASSERT_NE(nullptr, input);

    // File should be mapped and accessible
    EXPECT_EQ(file_size, static_cast<size_t>(input->length()));
    EXPECT_EQ(0, input->readByte());

    // Seek to various positions
    input->seek(1024 * 1024);
    EXPECT_EQ((1024 * 1024) & 0xFF, input->readByte());

    input->seek(2 * 1024 * 1024);
    EXPECT_EQ((2 * 1024 * 1024) & 0xFF, input->readByte());
}

#endif  // Windows

// ==================== Cross-Platform Tests ====================

TEST_F(MMapPlatformTest, ReadAdviceEnumValues) {
    // Ensure ReadAdvice enum has expected values
    using ReadAdvice = IOContext::ReadAdvice;

    ReadAdvice normal = ReadAdvice::NORMAL;
    ReadAdvice sequential = ReadAdvice::SEQUENTIAL;
    ReadAdvice random = ReadAdvice::RANDOM;

    // Values should be distinct
    EXPECT_NE(normal, sequential);
    EXPECT_NE(normal, random);
    EXPECT_NE(sequential, random);
}

TEST_F(MMapPlatformTest, PreloadConfigurationPersists) {
    auto dir = MMapDirectory::open(test_dir);

    // Default is false
    EXPECT_FALSE(dir->isPreload());

    // Set to true
    dir->setPreload(true);
    EXPECT_TRUE(dir->isPreload());

    // Set back to false
    dir->setPreload(false);
    EXPECT_FALSE(dir->isPreload());

    // Multiple toggles
    dir->setPreload(true);
    dir->setPreload(true);
    EXPECT_TRUE(dir->isPreload());
}

TEST_F(MMapPlatformTest, IOContextStaticInstances) {
    // Verify static IOContext instances exist and have correct types
    EXPECT_EQ(IOContext::Type::DEFAULT, IOContext::DEFAULT.type);
    EXPECT_EQ(IOContext::Type::READONCE, IOContext::READONCE.type);
    EXPECT_EQ(IOContext::Type::READ, IOContext::READ.type);
    EXPECT_EQ(IOContext::Type::MERGE, IOContext::MERGE.type);
    EXPECT_EQ(IOContext::Type::FLUSH, IOContext::FLUSH.type);
}

TEST_F(MMapPlatformTest, ForMergeAndForFlush) {
    // Test factory methods
    int64_t merge_size = 100 * 1024 * 1024;  // 100MB
    IOContext merge_ctx = IOContext::forMerge(merge_size);
    EXPECT_EQ(IOContext::Type::MERGE, merge_ctx.type);
    EXPECT_EQ(merge_size, merge_ctx.mergeSize);
    EXPECT_EQ(IOContext::ReadAdvice::SEQUENTIAL, merge_ctx.getReadAdvice());

    int64_t flush_size = 50 * 1024 * 1024;  // 50MB
    IOContext flush_ctx = IOContext::forFlush(flush_size);
    EXPECT_EQ(IOContext::Type::FLUSH, flush_ctx.type);
    EXPECT_EQ(flush_size, flush_ctx.flushSize);
    EXPECT_EQ(IOContext::ReadAdvice::SEQUENTIAL, flush_ctx.getReadAdvice());
}

// ==================== Performance Characteristics ====================

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)

TEST_F(MMapPlatformTest, SequentialReadWithHint) {
    // Create a moderately large file
    size_t file_size = 10 * 1024 * 1024;  // 10MB
    createTestFile("sequential.bin", file_size);

    auto dir = MMapDirectory::open(test_dir);

    // Open with SEQUENTIAL hint
    auto input = dir->openInput("sequential.bin", IOContext(IOContext::Type::READONCE));

    // Read sequentially
    uint8_t buffer[4096];
    size_t total_read = 0;

    while (total_read < file_size) {
        size_t to_read = std::min(sizeof(buffer), file_size - total_read);
        input->readBytes(buffer, to_read);
        total_read += to_read;
    }

    EXPECT_EQ(file_size, total_read);
}

TEST_F(MMapPlatformTest, RandomReadWithHint) {
    // Create test file
    size_t file_size = 10 * 1024 * 1024;  // 10MB
    createTestFile("random.bin", file_size);

    auto dir = MMapDirectory::open(test_dir);

    // Open with RANDOM hint
    auto input = dir->openInput("random.bin", IOContext(IOContext::Type::READ));

    // Random seeks
    std::vector<int64_t> positions = {0, 1024 * 1024, 5 * 1024 * 1024, 2 * 1024 * 1024,
                                      9 * 1024 * 1024};

    for (auto pos : positions) {
        input->seek(pos);
        uint8_t value = input->readByte();
        EXPECT_EQ(pos & 0xFF, value);
    }
}

#endif  // POSIX platforms
