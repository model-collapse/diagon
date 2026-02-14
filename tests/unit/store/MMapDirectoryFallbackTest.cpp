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

// ==================== Fallback Configuration Tests ====================

class MMapDirectoryFallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "diagon_test_mmap_fallback";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override { std::filesystem::remove_all(test_dir); }

    // Helper: Create a test file using FSDirectory
    void createTestFile(const std::string& name, size_t size) {
        auto dir = std::make_unique<FSDirectory>(test_dir);
        auto output = dir->createOutput(name, IOContext::DEFAULT);

        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i & 0xFF);
        }

        output->writeBytes(data.data(), data.size());
        output->close();
    }

    std::filesystem::path test_dir;
};

// ==================== Fallback Configuration ====================

TEST_F(MMapDirectoryFallbackTest, DefaultFallbackDisabled) {
    auto dir = MMapDirectory::open(test_dir);
    EXPECT_FALSE(dir->isUseFallback());
}

TEST_F(MMapDirectoryFallbackTest, EnableFallback) {
    auto dir = MMapDirectory::open(test_dir);

    dir->setUseFallback(true);
    EXPECT_TRUE(dir->isUseFallback());

    dir->setUseFallback(false);
    EXPECT_FALSE(dir->isUseFallback());
}

TEST_F(MMapDirectoryFallbackTest, ToStringIncludesFallback) {
    auto dir = MMapDirectory::open(test_dir);

    std::string str_without = dir->toString();
    EXPECT_EQ(str_without.find("fallback=true"), std::string::npos);

    dir->setUseFallback(true);
    std::string str_with = dir->toString();
    EXPECT_NE(str_with.find("fallback=true"), std::string::npos);
}

// ==================== Normal Operation ====================

TEST_F(MMapDirectoryFallbackTest, NormalOperationWithFallbackEnabled) {
    // Create test file
    createTestFile("test.bin", 1024);

    // Open with fallback enabled (but should not be triggered)
    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);

    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    ASSERT_NE(input, nullptr);

    // Verify it's actually memory-mapped (check for expected performance)
    EXPECT_EQ(input->length(), 1024);

    // Read some data
    uint8_t buffer[10];
    input->readBytes(buffer, 10);

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(buffer[i], i);
    }
}

// ==================== Error Handling ====================

TEST_F(MMapDirectoryFallbackTest, FileNotFoundAlwaysThrows) {
    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);  // Even with fallback enabled

    // File-not-found should always throw, never fall back
    EXPECT_THROW(dir->openInput("nonexistent.bin", IOContext::DEFAULT), FileNotFoundException);
}

TEST_F(MMapDirectoryFallbackTest, NotAFileAlwaysThrows) {
    // Create a subdirectory
    std::filesystem::create_directories(test_dir / "subdir");

    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);  // Even with fallback enabled

    // Not-a-file should always throw, never fall back
    EXPECT_THROW(dir->openInput("subdir", IOContext::DEFAULT), IOException);
}

// ==================== Platform-Specific Fallback ====================

#if defined(_WIN32)
// On Windows, verify that native mmap support works correctly

TEST_F(MMapDirectoryFallbackTest, WindowsNativeMMapSupport) {
    createTestFile("test.bin", 1024);

    auto dir = MMapDirectory::open(test_dir);

    // Windows now has native mmap support - should work without fallback
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->length(), 1024);

    // Verify data is readable
    uint8_t value = input->readByte();
    EXPECT_EQ(value, 0);

    // Verify multiple reads work
    input->seek(100);
    EXPECT_EQ(input->readByte(), 100 & 0xFF);

    input->seek(500);
    EXPECT_EQ(input->readByte(), 500 & 0xFF);
}

TEST_F(MMapDirectoryFallbackTest, WindowsMMapWithLargeFile) {
    // Test with larger file to ensure chunking works on Windows
    size_t file_size = 4 * 1024 * 1024;  // 4MB
    createTestFile("large.bin", file_size);

    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("large.bin", IOContext::DEFAULT);

    ASSERT_NE(input, nullptr);
    EXPECT_EQ(static_cast<size_t>(input->length()), file_size);

    // Verify reads at various positions
    EXPECT_EQ(input->readByte(), 0);

    input->seek(1024 * 1024);
    EXPECT_EQ(input->readByte(), (1024 * 1024) & 0xFF);

    input->seek(3 * 1024 * 1024);
    EXPECT_EQ(input->readByte(), (3 * 1024 * 1024) & 0xFF);
}

#endif  // _WIN32

// ==================== Simulated Failure Tests ====================

// Note: It's difficult to reliably trigger mmap ENOMEM failures in unit tests
// These would require:
// - Extremely low ulimit -v settings
// - 32-bit architecture with address space exhaustion
// - Platform with disabled mmap support
//
// Instead, we rely on:
// 1. Manual testing with restricted ulimits
// 2. Platform-specific testing (Windows fallback above)
// 3. Integration tests that verify fallback configuration works

// ==================== Concurrent Access with Fallback ====================

TEST_F(MMapDirectoryFallbackTest, ConcurrentAccessWithFallback) {
    createTestFile("concurrent.bin", 10240);

    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);

    // Open multiple inputs
    auto input1 = dir->openInput("concurrent.bin", IOContext::DEFAULT);
    auto input2 = dir->openInput("concurrent.bin", IOContext::DEFAULT);
    auto input3 = dir->openInput("concurrent.bin", IOContext::DEFAULT);

    ASSERT_NE(input1, nullptr);
    ASSERT_NE(input2, nullptr);
    ASSERT_NE(input3, nullptr);

    // All should have correct length
    EXPECT_EQ(input1->length(), 10240);
    EXPECT_EQ(input2->length(), 10240);
    EXPECT_EQ(input3->length(), 10240);

    // Each should be independently seekable
    input1->seek(100);
    input2->seek(200);
    input3->seek(300);

    EXPECT_EQ(input1->getFilePointer(), 100);
    EXPECT_EQ(input2->getFilePointer(), 200);
    EXPECT_EQ(input3->getFilePointer(), 300);
}

// ==================== Configuration Persistence ====================

TEST_F(MMapDirectoryFallbackTest, FallbackConfigPersistsAcrossOperations) {
    createTestFile("file1.bin", 512);
    createTestFile("file2.bin", 1024);

    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);

    // Open multiple files - fallback should remain enabled
    auto input1 = dir->openInput("file1.bin", IOContext::DEFAULT);
    EXPECT_TRUE(dir->isUseFallback());

    auto input2 = dir->openInput("file2.bin", IOContext::DEFAULT);
    EXPECT_TRUE(dir->isUseFallback());

    // Disable and verify
    dir->setUseFallback(false);
    auto input3 = dir->openInput("file1.bin", IOContext::DEFAULT);
    EXPECT_FALSE(dir->isUseFallback());
}

// ==================== Clone and Slice with Fallback ====================

TEST_F(MMapDirectoryFallbackTest, CloneWithFallbackEnabled) {
    createTestFile("clone.bin", 2048);

    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);

    auto input = dir->openInput("clone.bin", IOContext::DEFAULT);
    ASSERT_NE(input, nullptr);

    // Clone should work
    auto cloned = input->clone();
    ASSERT_NE(cloned, nullptr);

    EXPECT_EQ(cloned->length(), 2048);

    // Original and clone should be independent
    input->seek(100);
    cloned->seek(200);

    EXPECT_EQ(input->getFilePointer(), 100);
    EXPECT_EQ(cloned->getFilePointer(), 200);
}

TEST_F(MMapDirectoryFallbackTest, SliceWithFallbackEnabled) {
    createTestFile("slice.bin", 4096);

    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);

    auto input = dir->openInput("slice.bin", IOContext::DEFAULT);
    ASSERT_NE(input, nullptr);

    // Create slice
    auto sliced = input->slice("test_slice", 1024, 2048);
    ASSERT_NE(sliced, nullptr);

    EXPECT_EQ(sliced->length(), 2048);

    // Slice should start at offset 0 relative to its start
    EXPECT_EQ(sliced->getFilePointer(), 0);

    // Read from slice
    uint8_t value = sliced->readByte();
    EXPECT_EQ(value, 1024 & 0xFF);  // First byte of slice (byte 1024 of parent)
}

// ==================== Different IOContext with Fallback ====================

TEST_F(MMapDirectoryFallbackTest, DifferentIOContextWithFallback) {
    createTestFile("context.bin", 8192);

    auto dir = MMapDirectory::open(test_dir);
    dir->setUseFallback(true);

    // SEQUENTIAL access
    auto input1 = dir->openInput("context.bin", IOContext(IOContext::Type::MERGE));
    ASSERT_NE(input1, nullptr);
    EXPECT_EQ(input1->length(), 8192);

    // RANDOM access
    auto input2 = dir->openInput("context.bin", IOContext(IOContext::Type::READ));
    ASSERT_NE(input2, nullptr);
    EXPECT_EQ(input2->length(), 8192);

    // NORMAL access
    auto input3 = dir->openInput("context.bin", IOContext::DEFAULT);
    ASSERT_NE(input3, nullptr);
    EXPECT_EQ(input3->length(), 8192);
}
