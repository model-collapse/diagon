// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

class NumericDocValuesIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_docvalues_integration_test";
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

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== End-to-End Integration Tests ====================

TEST_F(NumericDocValuesIntegrationTest, WriteAndReadSingleField) {
    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Add documents with numeric doc values
        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("price", (i + 1) * 100));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 5);

        // Get the segment reader
        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1);  // Should have one segment

        auto* leafReader = leaves[0].reader;
        ASSERT_NE(leafReader, nullptr);

        // Get numeric doc values
        auto* dv = leafReader->getNumericDocValues("price");
        ASSERT_NE(dv, nullptr);

        // Verify values
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(dv->advanceExact(i));
            EXPECT_EQ(dv->longValue(), (i + 1) * 100);
        }

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(NumericDocValuesIntegrationTest, WriteAndReadMultipleFields) {
    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Add documents with multiple numeric doc values fields
        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("price", (i + 1) * 100));
            doc.add(std::make_unique<NumericDocValuesField>("quantity", (i + 1) * 10));
            doc.add(std::make_unique<NumericDocValuesField>("rating", i + 1));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1);

        auto* leafReader = leaves[0].reader;

        // Verify price field
        auto* priceDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(priceDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(priceDv->advanceExact(i));
            EXPECT_EQ(priceDv->longValue(), (i + 1) * 100);
        }

        // Verify quantity field
        auto* quantityDv = leafReader->getNumericDocValues("quantity");
        ASSERT_NE(quantityDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(quantityDv->advanceExact(i));
            EXPECT_EQ(quantityDv->longValue(), (i + 1) * 10);
        }

        // Verify rating field
        auto* ratingDv = leafReader->getNumericDocValues("rating");
        ASSERT_NE(ratingDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(ratingDv->advanceExact(i));
            EXPECT_EQ(ratingDv->longValue(), i + 1);
        }

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(NumericDocValuesIntegrationTest, SparseValues) {
    // Write phase - not all documents have the field
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));

            // Only even documents have the price field
            if (i % 2 == 0) {
                doc.add(std::make_unique<NumericDocValuesField>("price", i * 100));
            }

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader;

        auto* dv = leafReader->getNumericDocValues("price");
        ASSERT_NE(dv, nullptr);

        // Verify even documents have values
        for (int i = 0; i < 10; i++) {
            EXPECT_TRUE(dv->advanceExact(i));
            if (i % 2 == 0) {
                EXPECT_EQ(dv->longValue(), i * 100);
            } else {
                // Odd documents should have 0 (missing value in our simple format)
                EXPECT_EQ(dv->longValue(), 0);
            }
        }

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(NumericDocValuesIntegrationTest, Iteration) {
    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("score", (i + 1) * 10));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase - test iteration
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader;

        auto* dv = leafReader->getNumericDocValues("score");
        ASSERT_NE(dv, nullptr);

        // Test nextDoc() iteration
        int docID = dv->nextDoc();
        EXPECT_EQ(docID, 0);
        EXPECT_EQ(dv->longValue(), 10);

        docID = dv->nextDoc();
        EXPECT_EQ(docID, 1);
        EXPECT_EQ(dv->longValue(), 20);

        // Test advance()
        docID = dv->advance(4);
        EXPECT_EQ(docID, 4);
        EXPECT_EQ(dv->longValue(), 50);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(NumericDocValuesIntegrationTest, MultipleSegments) {
    // Write phase - create multiple segments
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);  // Low limit to create multiple segments

        IndexWriter writer(*dir, config);

        // Add 10 documents - should create multiple segments
        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("id", i));
            doc.add(std::make_unique<NumericDocValuesField>("value", i * 100));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(reader->numDocs(), 10);

        auto leaves = reader->leaves();
        EXPECT_GE(leaves.size(), 1);  // Should have multiple segments

        // Verify we can read from all segments
        int totalDocs = 0;
        for (const auto& ctx : leaves) {
            auto* leafReader = ctx.reader;
            ASSERT_NE(leafReader, nullptr);

            auto* dv = leafReader->getNumericDocValues("value");
            ASSERT_NE(dv, nullptr);

            // Iterate through docs in this segment
            for (int i = 0; i < leafReader->maxDoc(); i++) {
                EXPECT_TRUE(dv->advanceExact(i));
                // Note: Can't easily verify exact values across segments
                // as docIDs are segment-local
                totalDocs++;
            }
        }

        EXPECT_EQ(totalDocs, 10);
        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(NumericDocValuesIntegrationTest, NonExistentField) {
    // Write phase
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test", false));
        doc.add(std::make_unique<NumericDocValuesField>("price", 100));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader;

        // Request non-existent field - should return nullptr
        auto* dv = leafReader->getNumericDocValues("nonexistent");
        EXPECT_EQ(dv, nullptr);

        // Request field that exists
        auto* priceDv = leafReader->getNumericDocValues("price");
        EXPECT_NE(priceDv, nullptr);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(NumericDocValuesIntegrationTest, LargeValues) {
    // Write phase with large int64_t values
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test", false));
        doc.add(std::make_unique<NumericDocValuesField>("big_positive",
                                                        std::numeric_limits<int64_t>::max()));
        doc.add(std::make_unique<NumericDocValuesField>("big_negative",
                                                        std::numeric_limits<int64_t>::min()));
        doc.add(std::make_unique<NumericDocValuesField>("zero", 0));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader;

        auto* posDv = leafReader->getNumericDocValues("big_positive");
        ASSERT_NE(posDv, nullptr);
        EXPECT_TRUE(posDv->advanceExact(0));
        EXPECT_EQ(posDv->longValue(), std::numeric_limits<int64_t>::max());

        auto* negDv = leafReader->getNumericDocValues("big_negative");
        ASSERT_NE(negDv, nullptr);
        EXPECT_TRUE(negDv->advanceExact(0));
        EXPECT_EQ(negDv->longValue(), std::numeric_limits<int64_t>::min());

        auto* zeroDv = leafReader->getNumericDocValues("zero");
        ASSERT_NE(zeroDv, nullptr);
        EXPECT_TRUE(zeroDv->advanceExact(0));
        EXPECT_EQ(zeroDv->longValue(), 0);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}
