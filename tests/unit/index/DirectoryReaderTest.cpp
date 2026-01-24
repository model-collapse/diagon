// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>
#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class DirectoryReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique directory for each test
        static int testCounter = 0;
        testDir_ = fs::temp_directory_path() / ("diagon_directory_reader_test_" + std::to_string(testCounter++));
        fs::create_directories(testDir_);
        dir = FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        if (dir) {
            dir->close();
        }
        if (fs::exists(testDir_)) {
            fs::remove_all(testDir_);
        }
    }

    // Helper: Write test index with multiple segments
    void writeTestIndex(int numDocsPerSegment, int numSegments) {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(numDocsPerSegment);  // Force flush after each segment
        IndexWriter writer(*dir, config);

        for (int seg = 0; seg < numSegments; seg++) {
            for (int i = 0; i < numDocsPerSegment; i++) {
                Document doc;

                // Create field type for indexed text
                FieldType ft;
                ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
                ft.stored = true;
                ft.tokenized = true;

                doc.add(std::make_unique<Field>(
                    "body",
                    "segment" + std::to_string(seg) + " doc" + std::to_string(i),
                    ft));
                writer.addDocument(doc);
            }
            writer.flush();  // Force new segment
        }

        writer.commit();
        // Destructor will close
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== Basic Open Tests ====================

TEST_F(DirectoryReaderTest, OpenDirectory) {
    // Write index with 2 segments
    writeTestIndex(5, 2);

    // Open reader
    auto reader = DirectoryReader::open(*dir);

    EXPECT_NE(reader, nullptr);
    EXPECT_EQ(reader->maxDoc(), 10);  // 2 segments * 5 docs
    EXPECT_EQ(reader->numDocs(), 10);
    EXPECT_FALSE(reader->hasDeletions());
}

TEST_F(DirectoryReaderTest, GetDirectory) {
    writeTestIndex(3, 1);

    auto reader = DirectoryReader::open(*dir);

    EXPECT_EQ(&reader->directory(), dir.get());
}

TEST_F(DirectoryReaderTest, GetSegmentInfos) {
    writeTestIndex(5, 2);

    auto reader = DirectoryReader::open(*dir);

    const auto& segmentInfos = reader->getSegmentInfos();
    EXPECT_GE(segmentInfos.size(), 1);  // At least 1 segment
    EXPECT_EQ(segmentInfos.totalMaxDoc(), 10);
}

// ==================== Segment Access Tests ====================

TEST_F(DirectoryReaderTest, GetSequentialSubReaders) {
    writeTestIndex(5, 2);

    auto reader = DirectoryReader::open(*dir);

    auto subReaders = reader->getSequentialSubReaders();
    EXPECT_GE(subReaders.size(), 1);

    // Check each sub-reader
    int totalDocs = 0;
    for (const auto& subReader : subReaders) {
        EXPECT_NE(subReader, nullptr);
        totalDocs += subReader->maxDoc();
    }
    EXPECT_EQ(totalDocs, 10);
}

TEST_F(DirectoryReaderTest, GetLeaves) {
    writeTestIndex(5, 2);

    auto reader = DirectoryReader::open(*dir);

    auto leaves = reader->leaves();
    EXPECT_GE(leaves.size(), 1);

    // Check leaf contexts
    int totalDocs = 0;
    int expectedDocBase = 0;
    for (int i = 0; i < static_cast<int>(leaves.size()); i++) {
        const auto& ctx = leaves[i];

        EXPECT_NE(ctx.reader(), nullptr);
        EXPECT_EQ(ctx.ord(), i);
        EXPECT_EQ(ctx.docBase(), expectedDocBase);

        totalDocs += ctx.reader()->maxDoc();
        expectedDocBase += ctx.reader()->maxDoc();
    }
    EXPECT_EQ(totalDocs, 10);
}

// ==================== Statistics Tests ====================

TEST_F(DirectoryReaderTest, MaxDocAggregation) {
    writeTestIndex(7, 3);  // 21 docs total

    auto reader = DirectoryReader::open(*dir);

    EXPECT_EQ(reader->maxDoc(), 21);
}

TEST_F(DirectoryReaderTest, NumDocsAggregation) {
    writeTestIndex(4, 2);  // 8 docs total

    auto reader = DirectoryReader::open(*dir);

    EXPECT_EQ(reader->numDocs(), 8);
}

TEST_F(DirectoryReaderTest, HasDeletions) {
    writeTestIndex(5, 2);

    auto reader = DirectoryReader::open(*dir);

    // Phase 4: No deletions support
    EXPECT_FALSE(reader->hasDeletions());
}

// ==================== Terms Access Tests ====================

TEST_F(DirectoryReaderTest, AccessTermsViaLeaves) {
    writeTestIndex(3, 2);

    auto reader = DirectoryReader::open(*dir);

    // Access terms through leaf readers
    auto leaves = reader->leaves();
    for (const auto& ctx : leaves) {
        auto leafReader = ctx.reader();

        // Get terms for "_all" field (Phase 3: all fields combined)
        auto terms = leafReader->terms("_all");
        EXPECT_NE(terms, nullptr);
        EXPECT_GT(terms->size(), 0);
    }
}

// ==================== Lifecycle Tests ====================

TEST_F(DirectoryReaderTest, CloseDirectoryReader) {
    writeTestIndex(5, 1);

    auto reader = DirectoryReader::open(*dir);

    // Should be able to access before close
    EXPECT_EQ(reader->maxDoc(), 5);

    // Close by decrementing ref count
    reader->decRef();

    // After close, operations should throw
    EXPECT_THROW(reader->maxDoc(), AlreadyClosedException);
}

TEST_F(DirectoryReaderTest, RefCounting) {
    writeTestIndex(5, 1);

    auto reader = DirectoryReader::open(*dir);

    // Initial ref count should be 1
    EXPECT_EQ(reader->getRefCount(), 1);

    // Increment
    reader->incRef();
    EXPECT_EQ(reader->getRefCount(), 2);

    // Still accessible
    EXPECT_EQ(reader->maxDoc(), 5);

    // Decrement
    reader->decRef();
    EXPECT_EQ(reader->getRefCount(), 1);

    // Still accessible
    EXPECT_EQ(reader->maxDoc(), 5);

    // Final decrement closes
    reader->decRef();
    EXPECT_EQ(reader->getRefCount(), 0);
}

TEST_F(DirectoryReaderTest, SegmentReadersAreClosedOnDirectoryReaderClose) {
    writeTestIndex(3, 2);

    auto reader = DirectoryReader::open(*dir);

    // Get a leaf reader
    auto leaves = reader->leaves();
    ASSERT_GT(leaves.size(), 0);
    auto leafReader = leaves[0].reader();

    // Leaf should be accessible
    EXPECT_GT(leafReader->maxDoc(), 0);

    // Close directory reader
    reader->decRef();

    // Leaf reader should also be closed
    EXPECT_THROW(leafReader->maxDoc(), AlreadyClosedException);
}
