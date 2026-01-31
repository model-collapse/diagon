// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Integration test for Lucene104 codec
 *
 * Validates end-to-end indexing and searching with Lucene104 format.
 */

#include "diagon/codecs/Codec.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/document/FieldType.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/IndexWriterConfig.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/TopScoreDocCollector.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>

using namespace diagon;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::store;

namespace {

class Lucene104IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test
        testDir_ = "/tmp/lucene104_integration_test";
        std::filesystem::remove_all(testDir_);
        std::filesystem::create_directories(testDir_);
    }

    void TearDown() override {
        // Clean up
        std::filesystem::remove_all(testDir_);
    }

    std::string testDir_;
};

TEST_F(Lucene104IntegrationTest, BasicIndexingAndSearch) {
    // Create directory
    auto dir = std::make_shared<FSDirectory>(testDir_);

    // Create index writer with Lucene104 codec
    IndexWriterConfig config;
    config.setCodec(codecs::Codec::forName("Lucene104"));

    auto writer = std::make_unique<IndexWriter>(dir, config);

    // Add documents
    for (int i = 0; i < 100; i++) {
        Document doc;

        // Add text field
        auto fieldType = std::make_unique<FieldType>();
        fieldType->setStored(true);
        fieldType->setTokenized(true);
        fieldType->setIndexOptions(IndexOptions::DOCS_AND_FREQS);

        std::string text = "document " + std::to_string(i) + " contains searchable text";
        if (i % 10 == 0) {
            text += " special";  // Every 10th doc has "special" term
        }

        doc.add(std::make_unique<Field>("body", text, std::move(fieldType)));

        writer->addDocument(doc);
    }

    // Commit to create segment
    writer->commit();
    writer->close();

    // Verify .doc file was created
    auto segmentFiles = std::filesystem::directory_iterator(testDir_);
    bool foundDocFile = false;
    for (const auto& entry : segmentFiles) {
        if (entry.path().extension() == ".doc") {
            foundDocFile = true;
            std::cout << "Found .doc file: " << entry.path().filename() << std::endl;

            // Check file size is reasonable (should have data)
            auto fileSize = std::filesystem::file_size(entry.path());
            EXPECT_GT(fileSize, 100) << ".doc file is too small";
            std::cout << "  Size: " << fileSize << " bytes" << std::endl;
        }
    }
    EXPECT_TRUE(foundDocFile) << "No .doc file created";

    // Open reader and search
    auto reader = DirectoryReader::open(dir);
    IndexSearcher searcher(reader);

    // Search for "special" term
    TermQuery query("body", "special");
    auto collector = std::make_shared<TopScoreDocCollector>(20);

    searcher.search(&query, collector);

    auto topDocs = collector->topDocs();

    // Should find 10 documents (0, 10, 20, ..., 90)
    EXPECT_EQ(topDocs.totalHits, 10) << "Expected 10 hits for 'special' term";
    EXPECT_EQ(topDocs.scoreDocs.size(), 10);

    // Verify doc IDs are correct
    std::set<int> expectedDocs = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    std::set<int> actualDocs;
    for (const auto& scoreDoc : topDocs.scoreDocs) {
        actualDocs.insert(scoreDoc.doc);
    }
    EXPECT_EQ(actualDocs, expectedDocs) << "Incorrect document IDs returned";

    std::cout << "Found " << topDocs.totalHits << " documents with 'special' term" << std::endl;
}

TEST_F(Lucene104IntegrationTest, LargerDataset) {
    // Create directory
    auto dir = std::make_shared<FSDirectory>(testDir_);

    // Create index writer with Lucene104 codec
    IndexWriterConfig config;
    config.setCodec(codecs::Codec::forName("Lucene104"));

    auto writer = std::make_unique<IndexWriter>(dir, config);

    // Add 1000 documents
    for (int i = 0; i < 1000; i++) {
        Document doc;

        auto fieldType = std::make_unique<FieldType>();
        fieldType->setStored(true);
        fieldType->setTokenized(true);
        fieldType->setIndexOptions(IndexOptions::DOCS_AND_FREQS);

        std::string text = "document " + std::to_string(i) + " searchable";
        doc.add(std::make_unique<Field>("body", text, std::move(fieldType)));

        writer->addDocument(doc);
    }

    // Commit to create segment
    writer->commit();
    writer->close();

    // Open reader and verify
    auto reader = DirectoryReader::open(dir);
    EXPECT_EQ(reader->maxDoc(), 1000) << "Expected 1000 documents";

    // Search for common term
    IndexSearcher searcher(reader);
    TermQuery query("body", "searchable");
    auto collector = std::make_shared<TopScoreDocCollector>(1000);

    searcher.search(&query, collector);

    auto topDocs = collector->topDocs();
    EXPECT_EQ(topDocs.totalHits, 1000) << "All docs should match 'searchable'";

    std::cout << "Successfully indexed and searched 1000 documents" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
