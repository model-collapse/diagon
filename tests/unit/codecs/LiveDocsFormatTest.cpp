// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/LiveDocsFormat.h"

#include "diagon/store/FSDirectory.h"
#include "diagon/util/BitSet.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace diagon::codecs;
using namespace diagon::store;
using namespace diagon::util;

// Helper function to create a temporary directory
static std::string createTempDir() {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path();
    tempPath /= "diagon_livedocs_test_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(tempPath);
    return tempPath.string();
}

// Helper function to remove directory and all contents
static void removeDir(const std::string& path) {
    std::filesystem::remove_all(path);
}

// ==================== LiveDocsFormat Tests ====================

TEST(LiveDocsFormatTest, WriteAndReadBasic) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_test";
    int maxDoc = 100;
    int delCount = 10;

    // Create live docs bitset (1 = live, 0 = deleted)
    BitSet liveDocs(maxDoc);
    for (int i = 0; i < maxDoc; i++) {
        liveDocs.set(i);  // All live initially
    }

    // Delete some documents (set bits to 0)
    liveDocs.clear(5);
    liveDocs.clear(15);
    liveDocs.clear(25);
    liveDocs.clear(35);
    liveDocs.clear(45);
    liveDocs.clear(55);
    liveDocs.clear(65);
    liveDocs.clear(75);
    liveDocs.clear(85);
    liveDocs.clear(95);

    // Write live docs
    format.writeLiveDocs(*directory, segmentName, liveDocs, delCount);

    // Read live docs back
    auto readLiveDocs = format.readLiveDocs(*directory, segmentName, maxDoc);
    ASSERT_NE(readLiveDocs, nullptr);

    // Verify all bits match
    EXPECT_EQ(readLiveDocs->length(), maxDoc);
    for (int i = 0; i < maxDoc; i++) {
        EXPECT_EQ(readLiveDocs->get(i), liveDocs.get(i)) << "Bit mismatch at index " << i;
    }

    directory->close();
    removeDir(tempDir);
}

TEST(LiveDocsFormatTest, AllLive) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_test";
    int maxDoc = 50;

    // All documents live
    BitSet liveDocs(maxDoc);
    for (int i = 0; i < maxDoc; i++) {
        liveDocs.set(i);
    }

    // Write live docs
    format.writeLiveDocs(*directory, segmentName, liveDocs, 0);

    // Read back
    auto readLiveDocs = format.readLiveDocs(*directory, segmentName, maxDoc);
    ASSERT_NE(readLiveDocs, nullptr);

    // Verify all bits set
    for (int i = 0; i < maxDoc; i++) {
        EXPECT_TRUE(readLiveDocs->get(i)) << "Expected bit " << i << " to be set";
    }

    directory->close();
    removeDir(tempDir);
}

TEST(LiveDocsFormatTest, AllDeleted) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_test";
    int maxDoc = 50;

    // All documents deleted
    BitSet liveDocs(maxDoc);
    // Don't set any bits - all clear (deleted)

    // Write live docs
    format.writeLiveDocs(*directory, segmentName, liveDocs, maxDoc);

    // Read back
    auto readLiveDocs = format.readLiveDocs(*directory, segmentName, maxDoc);
    ASSERT_NE(readLiveDocs, nullptr);

    // Verify all bits clear
    for (int i = 0; i < maxDoc; i++) {
        EXPECT_FALSE(readLiveDocs->get(i)) << "Expected bit " << i << " to be clear";
    }

    directory->close();
    removeDir(tempDir);
}

TEST(LiveDocsFormatTest, FileDoesNotExist) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_nonexistent";
    int maxDoc = 100;

    // Try to read non-existent file
    auto liveDocs = format.readLiveDocs(*directory, segmentName, maxDoc);
    EXPECT_EQ(liveDocs, nullptr);

    directory->close();
    removeDir(tempDir);
}

TEST(LiveDocsFormatTest, LiveDocsExist) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_test";
    int maxDoc = 10;

    // Initially file doesn't exist
    EXPECT_FALSE(format.liveDocsExist(*directory, segmentName));

    // Write live docs
    BitSet liveDocs(maxDoc);
    for (int i = 0; i < maxDoc; i++) {
        liveDocs.set(i);
    }
    format.writeLiveDocs(*directory, segmentName, liveDocs, 0);

    // Now file exists
    EXPECT_TRUE(format.liveDocsExist(*directory, segmentName));

    directory->close();
    removeDir(tempDir);
}

TEST(LiveDocsFormatTest, LargeDocument) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_test";
    int maxDoc = 10000;
    int delCount = 100;

    // Create live docs
    BitSet liveDocs(maxDoc);
    for (int i = 0; i < maxDoc; i++) {
        liveDocs.set(i);
    }

    // Delete every 100th document
    for (int i = 0; i < maxDoc; i += 100) {
        liveDocs.clear(i);
    }

    // Write and read
    format.writeLiveDocs(*directory, segmentName, liveDocs, delCount);
    auto readLiveDocs = format.readLiveDocs(*directory, segmentName, maxDoc);
    ASSERT_NE(readLiveDocs, nullptr);

    // Verify
    for (int i = 0; i < maxDoc; i++) {
        EXPECT_EQ(readLiveDocs->get(i), liveDocs.get(i));
    }

    directory->close();
    removeDir(tempDir);
}

TEST(LiveDocsFormatTest, Cardinality) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    LiveDocsFormat format;
    std::string segmentName = "_test";
    int maxDoc = 1000;
    int delCount = 100;

    // Create live docs with specific deletions
    BitSet liveDocs(maxDoc);
    for (int i = 0; i < maxDoc; i++) {
        liveDocs.set(i);
    }

    // Delete 100 documents
    for (int i = 0; i < 100; i++) {
        liveDocs.clear(i * 10);
    }

    size_t originalCardinality = liveDocs.cardinality();
    EXPECT_EQ(originalCardinality, maxDoc - 100);

    // Write and read
    format.writeLiveDocs(*directory, segmentName, liveDocs, delCount);
    auto readLiveDocs = format.readLiveDocs(*directory, segmentName, maxDoc);
    ASSERT_NE(readLiveDocs, nullptr);

    // Verify cardinality matches
    EXPECT_EQ(readLiveDocs->cardinality(), originalCardinality);

    directory->close();
    removeDir(tempDir);
}
