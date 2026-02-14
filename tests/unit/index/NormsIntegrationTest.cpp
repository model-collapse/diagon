// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

/**
 * End-to-end integration test for norms
 * Tests: IndexWriter writes norms → SegmentReader reads norms → Values are correct
 */
class NormsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_norms_integration_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override { fs::remove_all(testDir_); }

    /**
     * Calculate expected norm value for a given field length
     * Using same encoding as Lucene104NormsWriter
     */
    int64_t calculateExpectedNorm(int64_t length) {
        if (length <= 0) {
            return 127;
        }
        double sqrtLength = std::sqrt(static_cast<double>(length));
        double encoded = 127.0 / sqrtLength;
        if (encoded > 127.0)
            return 127;
        if (encoded < -128.0)
            return -128;
        return static_cast<int64_t>(encoded);
    }

    fs::path testDir_;
};

TEST_F(NormsIntegrationTest, WriteAndReadNorms) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    // Write documents with varying lengths
    {
        IndexWriter writer(*dir, config);

        // Document 0: 1 term
        Document doc0;
        doc0.add(std::make_unique<TextField>("content", "word"));
        writer.addDocument(doc0);

        // Document 1: 4 terms
        Document doc1;
        doc1.add(std::make_unique<TextField>("content", "one two three four"));
        writer.addDocument(doc1);

        // Document 2: 9 terms
        Document doc2;
        doc2.add(
            std::make_unique<TextField>("content", "one two three four five six seven eight nine"));
        writer.addDocument(doc2);

        writer.commit();
        writer.close();
    }

    // Read documents and verify norms
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        ASSERT_EQ(1, leaves.size()) << "Should have one segment";

        auto& leafContext = leaves[0];
        auto* leafReader = dynamic_cast<SegmentReader*>(leafContext.reader);
        ASSERT_NE(nullptr, leafReader);

        // Get norms for "content" field
        auto* norms = leafReader->getNormValues("content");
        ASSERT_NE(nullptr, norms) << "Norms should be available for indexed field";

        // Verify norms for each document
        // Doc 0: 1 term → norm ≈ 127
        ASSERT_TRUE(norms->advanceExact(0));
        int64_t norm0 = norms->longValue();
        int64_t expected0 = calculateExpectedNorm(1);
        EXPECT_EQ(expected0, norm0) << "Doc 0 (1 term) norm mismatch";
        EXPECT_EQ(127, norm0) << "Single term should get max norm";

        // Doc 1: 4 terms → norm ≈ 63
        ASSERT_TRUE(norms->advanceExact(1));
        int64_t norm1 = norms->longValue();
        int64_t expected1 = calculateExpectedNorm(4);
        EXPECT_EQ(expected1, norm1) << "Doc 1 (4 terms) norm mismatch";
        EXPECT_NEAR(63, norm1, 1) << "4 terms should get norm ≈ 63";

        // Doc 2: 9 terms → norm ≈ 42
        ASSERT_TRUE(norms->advanceExact(2));
        int64_t norm2 = norms->longValue();
        int64_t expected2 = calculateExpectedNorm(9);
        EXPECT_EQ(expected2, norm2) << "Doc 2 (9 terms) norm mismatch";
        EXPECT_NEAR(42, norm2, 1) << "9 terms should get norm ≈ 42";

        // Verify norms decrease as document length increases
        EXPECT_GT(norm0, norm1) << "Shorter doc should have higher norm";
        EXPECT_GT(norm1, norm2) << "Shorter doc should have higher norm";

        // Reader closed automatically via RAII
    }
}

TEST_F(NormsIntegrationTest, NormsFilesCreated) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    IndexWriter writer(*dir, config);

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("field", "term" + std::to_string(i)));
        writer.addDocument(doc);
    }

    writer.commit();
    writer.close();

    // Verify norms files exist
    auto files = dir->listAll();
    bool hasNvd = false;
    bool hasNvm = false;

    for (const auto& file : files) {
        if (file.find(".nvd") != std::string::npos)
            hasNvd = true;
        if (file.find(".nvm") != std::string::npos)
            hasNvm = true;
    }

    EXPECT_TRUE(hasNvd) << "Norms data file (.nvd) should be created";
    EXPECT_TRUE(hasNvm) << "Norms metadata file (.nvm) should be created";
}

TEST_F(NormsIntegrationTest, EmptyFieldNorms) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        // Document with empty field
        Document doc0;
        doc0.add(std::make_unique<TextField>("content", ""));
        writer.addDocument(doc0);

        // Document with content (2 terms to get norm < 127)
        Document doc1;
        doc1.add(std::make_unique<TextField>("content", "one two"));
        writer.addDocument(doc1);

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto& leafContext = leaves[0];
        auto* leafReader = dynamic_cast<SegmentReader*>(leafContext.reader);
        auto* norms = leafReader->getNormValues("content");
        ASSERT_NE(nullptr, norms);

        // Empty field should get max norm (127)
        ASSERT_TRUE(norms->advanceExact(0));
        EXPECT_EQ(127, norms->longValue()) << "Empty field should get maximum norm";

        // Non-empty field should get lower norm
        ASSERT_TRUE(norms->advanceExact(1));
        EXPECT_LT(norms->longValue(), 127) << "Non-empty field should get lower norm";
    }
}

TEST_F(NormsIntegrationTest, NoNormsForNonExistentField) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("indexed", "content"));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto& leafContext = leaves[0];
        auto* leafReader = dynamic_cast<SegmentReader*>(leafContext.reader);

        // Non-existent field should not have norms
        auto* normsNonExistent = leafReader->getNormValues("non_existent");
        EXPECT_EQ(nullptr, normsNonExistent) << "Non-existent field should not have norms";

        // Indexed field should have norms
        auto* normsIndexed = leafReader->getNormValues("indexed");
        EXPECT_NE(nullptr, normsIndexed) << "Indexed field should have norms";
    }
}

TEST_F(NormsIntegrationTest, NormsAcrossMultipleSegments) {
    auto dir = FSDirectory::open(testDir_.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(2);  // Force multiple segments

    {
        IndexWriter writer(*dir, config);

        // Add 6 documents (will create 3 segments)
        for (int i = 0; i < 6; i++) {
            Document doc;
            std::string content;
            for (int j = 0; j <= i; j++) {
                content += "term" + std::to_string(j) + " ";
            }
            doc.add(std::make_unique<TextField>("content", content));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        EXPECT_GE(leaves.size(), 1) << "Should have at least one segment";

        // Verify norms in each segment
        for (const auto& leafContext : leaves) {
            auto* leafReader = dynamic_cast<SegmentReader*>(leafContext.reader);
            auto* norms = leafReader->getNormValues("content");
            ASSERT_NE(nullptr, norms) << "Each segment should have norms";
        }
    }
}
