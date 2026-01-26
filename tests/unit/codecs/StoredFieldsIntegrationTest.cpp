// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsReader.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace diagon::codecs;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::store;

// Helper function to create a temporary directory
static std::string createTempDir() {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path();
    tempPath /= "diagon_stored_fields_int_test_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(tempPath);
    return tempPath.string();
}

// Helper function to remove directory and all contents
static void removeDir(const std::string& path) {
    std::filesystem::remove_all(path);
}

// ==================== StoredFields Integration Tests ====================

TEST(StoredFieldsIntegrationTest, DWPTToSegmentReader) {
    // Create temporary directory
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Phase 1: Write documents using DocumentsWriterPerThread
    std::shared_ptr<SegmentInfo> segmentInfo;
    {
        DocumentsWriterPerThread::Config config;
        config.maxBufferedDocs = 10;
        config.ramBufferSizeMB = 16;

        DocumentsWriterPerThread dwpt(config, directory.get());

        // Create documents
        for (int i = 0; i < 3; i++) {
            Document doc;

            // Add stored string field
            FieldType stringType;
            stringType.stored = true;
            auto nameField =
                std::make_unique<Field>("name", "Person " + std::to_string(i), stringType);
            doc.add(std::move(nameField));

            // Add stored numeric fields
            FieldType numericType;
            numericType.stored = true;
            auto ageField = std::make_unique<Field>("age", int64_t(20 + i), numericType);
            doc.add(std::move(ageField));

            auto scoreField = std::make_unique<Field>("score", int64_t(1000 + i * 100), numericType);
            doc.add(std::move(scoreField));

            dwpt.addDocument(doc);
        }

        // Flush to create segment
        segmentInfo = dwpt.flush();
        ASSERT_NE(segmentInfo, nullptr);
    }

    // Phase 2: Read documents using SegmentReader
    {
        auto reader = SegmentReader::open(*directory, segmentInfo);

        // Get stored fields reader
        auto storedFieldsReader = reader->storedFieldsReader();
        ASSERT_NE(storedFieldsReader, nullptr);

        EXPECT_EQ(storedFieldsReader->numDocs(), 3);

        // Read each document and verify stored fields
        for (int i = 0; i < 3; i++) {
            auto fields = storedFieldsReader->document(i);

            // Verify we have all fields
            EXPECT_EQ(fields.size(), 3);

            // Verify name
            ASSERT_TRUE(fields.find("name") != fields.end());
            EXPECT_EQ(std::get<std::string>(fields["name"]), "Person " + std::to_string(i));

            // Verify age (stored as int64 in our implementation)
            ASSERT_TRUE(fields.find("age") != fields.end());
            EXPECT_EQ(std::get<int64_t>(fields["age"]), 20 + i);

            // Verify score
            ASSERT_TRUE(fields.find("score") != fields.end());
            EXPECT_EQ(std::get<int64_t>(fields["score"]), 1000 + i * 100);
        }

        // Release reader (decRef)
        reader->decRef();
    }

    // Clean up
    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsIntegrationTest, OnlyIndexedFields) {
    // Test with documents that have no stored fields
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    std::shared_ptr<SegmentInfo> segmentInfo;
    {
        DocumentsWriterPerThread::Config config;
        config.maxBufferedDocs = 10;

        DocumentsWriterPerThread dwpt(config, directory.get());

        // Create documents with only indexed fields (no stored fields)
        for (int i = 0; i < 2; i++) {
            Document doc;

            // Add indexed field (not stored)
            FieldType indexedType;
            indexedType.indexOptions = IndexOptions::DOCS;
            auto textField =
                std::make_unique<Field>("text", "content " + std::to_string(i), indexedType);
            doc.add(std::move(textField));

            dwpt.addDocument(doc);
        }

        segmentInfo = dwpt.flush();
        ASSERT_NE(segmentInfo, nullptr);
    }

    // Try to read stored fields (should have no .fdt/.fdx files)
    {
        auto reader = SegmentReader::open(*directory, segmentInfo);

        // storedFieldsReader() should return nullptr since no stored fields files exist
        auto storedFieldsReader = reader->storedFieldsReader();
        EXPECT_EQ(storedFieldsReader, nullptr);

        reader->decRef();
    }

    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsIntegrationTest, MixedStoredAndIndexed) {
    // Test documents with both stored and indexed fields
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    std::shared_ptr<SegmentInfo> segmentInfo;
    {
        DocumentsWriterPerThread::Config config;
        DocumentsWriterPerThread dwpt(config, directory.get());

        Document doc;

        // Add indexed field (not stored)
        FieldType indexedType;
        indexedType.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
        auto textField = std::make_unique<Field>("text", "searchable content", indexedType);
        doc.add(std::move(textField));

        // Add stored field (not indexed)
        FieldType storedType;
        storedType.stored = true;
        auto summaryField = std::make_unique<Field>("summary", "This is a summary", storedType);
        doc.add(std::move(summaryField));

        // Add both indexed and stored field
        FieldType bothType;
        bothType.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
        bothType.stored = true;
        auto titleField = std::make_unique<Field>("title", "Important Document", bothType);
        doc.add(std::move(titleField));

        // Add numeric doc values field (not stored)
        FieldType docValuesType;
        docValuesType.docValuesType = DocValuesType::NUMERIC;
        auto countField = std::make_unique<Field>("count", int64_t(42), docValuesType);
        doc.add(std::move(countField));

        dwpt.addDocument(doc);
        segmentInfo = dwpt.flush();
    }

    // Read back and verify
    {
        auto reader = SegmentReader::open(*directory, segmentInfo);

        auto storedFieldsReader = reader->storedFieldsReader();
        ASSERT_NE(storedFieldsReader, nullptr);

        auto fields = storedFieldsReader->document(0);

        // Should have summary and title (both stored), but not text (not stored) or count (doc values, not stored)
        EXPECT_EQ(fields.size(), 2);
        EXPECT_EQ(std::get<std::string>(fields["summary"]), "This is a summary");
        EXPECT_EQ(std::get<std::string>(fields["title"]), "Important Document");

        reader->decRef();
    }

    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsIntegrationTest, MultipleDocuments) {
    // Test reading multiple documents with random access
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    std::shared_ptr<SegmentInfo> segmentInfo;
    {
        DocumentsWriterPerThread::Config config;
        DocumentsWriterPerThread dwpt(config, directory.get());

        FieldType storedType;
        storedType.stored = true;

        // Write 10 documents
        for (int i = 0; i < 10; i++) {
            Document doc;

            auto idField = std::make_unique<Field>("id", int64_t(i), storedType);
            doc.add(std::move(idField));

            auto valueField =
                std::make_unique<Field>("value", "Document " + std::to_string(i), storedType);
            doc.add(std::move(valueField));

            dwpt.addDocument(doc);
        }

        segmentInfo = dwpt.flush();
    }

    // Read documents in random order
    {
        auto reader = SegmentReader::open(*directory, segmentInfo);
        auto storedFieldsReader = reader->storedFieldsReader();
        ASSERT_NE(storedFieldsReader, nullptr);

        EXPECT_EQ(storedFieldsReader->numDocs(), 10);

        // Read in order: 5, 2, 8, 0, 9
        auto doc5 = storedFieldsReader->document(5);
        EXPECT_EQ(std::get<int64_t>(doc5["id"]), 5);
        EXPECT_EQ(std::get<std::string>(doc5["value"]), "Document 5");

        auto doc2 = storedFieldsReader->document(2);
        EXPECT_EQ(std::get<int64_t>(doc2["id"]), 2);
        EXPECT_EQ(std::get<std::string>(doc2["value"]), "Document 2");

        auto doc8 = storedFieldsReader->document(8);
        EXPECT_EQ(std::get<int64_t>(doc8["id"]), 8);
        EXPECT_EQ(std::get<std::string>(doc8["value"]), "Document 8");

        auto doc0 = storedFieldsReader->document(0);
        EXPECT_EQ(std::get<int64_t>(doc0["id"]), 0);
        EXPECT_EQ(std::get<std::string>(doc0["value"]), "Document 0");

        auto doc9 = storedFieldsReader->document(9);
        EXPECT_EQ(std::get<int64_t>(doc9["id"]), 9);
        EXPECT_EQ(std::get<std::string>(doc9["value"]), "Document 9");

        reader->decRef();
    }

    directory->close();
    removeDir(tempDir);
}
