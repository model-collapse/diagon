// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentReader.h"

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

class SegmentReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique directory for each test
        static int testCounter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_segment_reader_test_" + std::to_string(testCounter++));
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

    // Helper: Write test index
    void writeTestIndex(int numDocs) {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;

            // Create field type for indexed text
            FieldType ft;
            ft.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
            ft.stored = true;
            ft.tokenized = true;

            doc.add(
                std::make_unique<Field>("body", "hello world test doc" + std::to_string(i), ft));
            writer.addDocument(doc);
        }

        writer.commit();
        // Destructor will close
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== Basic Open Tests ====================

TEST_F(SegmentReaderTest, OpenSegment) {
    // Write index
    writeTestIndex(5);

    // Read segments_N
    auto infos = SegmentInfos::readLatestCommit(*dir);
    ASSERT_GE(infos.size(), 1);

    // Open first segment
    auto segInfo = infos.info(0);
    auto reader = SegmentReader::open(*dir, segInfo);

    EXPECT_NE(reader, nullptr);
    EXPECT_EQ(reader->maxDoc(), 5);
    EXPECT_EQ(reader->numDocs(), 5);
    EXPECT_FALSE(reader->hasDeletions());
}

TEST_F(SegmentReaderTest, GetSegmentInfo) {
    writeTestIndex(3);

    auto infos = SegmentInfos::readLatestCommit(*dir);
    auto segInfo = infos.info(0);
    auto reader = SegmentReader::open(*dir, segInfo);

    EXPECT_EQ(reader->getSegmentInfo(), segInfo);
    EXPECT_EQ(reader->getSegmentName(), segInfo->name());
}

// ==================== Terms Access Tests ====================

TEST_F(SegmentReaderTest, GetTerms) {
    writeTestIndex(5);

    auto infos = SegmentInfos::readLatestCommit(*dir);
    auto reader = SegmentReader::open(*dir, infos.info(0));

    // Get terms for "body" field
    auto terms = reader->terms("body");
    EXPECT_NE(terms, nullptr);
    EXPECT_GT(terms->size(), 0);
}

TEST_F(SegmentReaderTest, GetTermsNonExistentField) {
    writeTestIndex(5);

    auto infos = SegmentInfos::readLatestCommit(*dir);
    auto reader = SegmentReader::open(*dir, infos.info(0));

    // Get terms for non-existent field
    auto terms = reader->terms("nonexistent");
    EXPECT_EQ(terms, nullptr);
}

TEST_F(SegmentReaderTest, IterateTermsAndPostings) {
    writeTestIndex(3);

    auto infos = SegmentInfos::readLatestCommit(*dir);
    auto reader = SegmentReader::open(*dir, infos.info(0));

    // Get terms for "body" field
    auto terms = reader->terms("body");
    ASSERT_NE(terms, nullptr);

    // Get iterator
    auto termsEnum = terms->iterator();
    ASSERT_NE(termsEnum, nullptr);

    // Iterate terms
    int termCount = 0;
    while (termsEnum->next()) {
        termCount++;

        // Check we can get postings
        auto postings = termsEnum->postings();
        EXPECT_NE(postings, nullptr);

        // Check we can iterate postings
        int docCount = 0;
        while (postings->nextDoc() != PostingsEnum::NO_MORE_DOCS) {
            docCount++;
        }
        EXPECT_GT(docCount, 0);
    }

    EXPECT_GT(termCount, 0);
}

// ==================== Lifecycle Tests ====================

TEST_F(SegmentReaderTest, CloseSegmentReader) {
    writeTestIndex(5);

    auto infos = SegmentInfos::readLatestCommit(*dir);
    auto reader = SegmentReader::open(*dir, infos.info(0));

    // Should be able to access before close
    EXPECT_EQ(reader->maxDoc(), 5);

    // Close by decrementing ref count
    reader->decRef();

    // After close, operations should throw
    EXPECT_THROW(reader->maxDoc(), AlreadyClosedException);
}

TEST_F(SegmentReaderTest, RefCounting) {
    writeTestIndex(5);

    auto infos = SegmentInfos::readLatestCommit(*dir);
    auto reader = SegmentReader::open(*dir, infos.info(0));

    // Initial ref count should be 1
    EXPECT_EQ(reader->getRefCount(), 1);

    // Increment
    reader->incRef();
    EXPECT_EQ(reader->getRefCount(), 2);

    // Can still access
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
