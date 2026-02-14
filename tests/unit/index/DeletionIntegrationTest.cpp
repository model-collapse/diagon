// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/index/Term.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::store;

// Helper function to create a temporary directory
static std::string createTempDir() {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path();
    tempPath /= "diagon_deletion_test_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(tempPath);
    return tempPath.string();
}

// Helper function to remove directory and all contents
static void removeDir(const std::string& path) {
    std::filesystem::remove_all(path);
}

// Helper function to add a text field to document
static void addTextField(Document& doc, const std::string& name, const std::string& value) {
    FieldType storedType;
    storedType.stored = true;
    storedType.indexOptions = diagon::index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;

    auto field = std::make_unique<Field>(name, value, storedType);
    doc.add(std::move(field));
}

// ==================== Deletion Integration Tests ====================

TEST(DeletionIntegrationTest, DeleteDocumentsByTerm) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    config.setMaxBufferedDocs(10);
    IndexWriter writer(*directory, config);

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        addTextField(doc, "id", std::to_string(i));
        addTextField(doc, "content", "document " + std::to_string(i));
        writer.addDocument(doc);
    }

    // Commit to create segment
    writer.commit();

    // Verify initial state
    auto reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 5);
    EXPECT_EQ(reader->maxDoc(), 5);
    reader->decRef();

    // Delete document with id=2
    Term term("id", "2");
    writer.deleteDocuments(term);
    writer.commit();

    // Verify deletion
    reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 4);  // One deleted
    EXPECT_EQ(reader->maxDoc(), 5);   // MaxDoc unchanged
    EXPECT_TRUE(reader->hasDeletions());

    reader->decRef();
    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, UpdateDocument) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    config.setMaxBufferedDocs(10);
    IndexWriter writer(*directory, config);

    // Add initial document
    Document doc1;
    addTextField(doc1, "id", "100");
    addTextField(doc1, "content", "original content");
    writer.addDocument(doc1);
    writer.commit();

    // Verify initial state
    auto reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 1);
    reader->decRef();

    // Update document (delete old, add new)
    Document doc2;
    addTextField(doc2, "id", "100");
    addTextField(doc2, "content", "updated content");
    Term term("id", "100");
    writer.updateDocument(term, doc2);
    writer.commit();

    // Verify update
    reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 1);  // Still 1 document
    EXPECT_EQ(reader->maxDoc(), 2);   // But maxDoc increased (old + new)
    EXPECT_TRUE(reader->hasDeletions());

    reader->decRef();
    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, MultipleDeletesInSameSegment) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    config.setMaxBufferedDocs(10);
    IndexWriter writer(*directory, config);

    // Add documents
    for (int i = 0; i < 10; i++) {
        Document doc;
        addTextField(doc, "id", std::to_string(i));
        addTextField(doc, "type", (i % 2 == 0) ? "even" : "odd");
        writer.addDocument(doc);
    }
    writer.commit();

    // Delete multiple documents (all even numbers)
    for (int i = 0; i < 10; i += 2) {
        Term term("id", std::to_string(i));
        writer.deleteDocuments(term);
    }
    writer.commit();

    // Verify deletions
    auto reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 5);  // 5 odd numbers left
    EXPECT_EQ(reader->maxDoc(), 10);

    reader->decRef();
    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, DeleteAcrossMultipleSegments) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer with small buffer to force multiple segments
    IndexWriterConfig config;
    config.setMaxBufferedDocs(3);  // Force flush after 3 docs
    IndexWriter writer(*directory, config);

    // Add documents across multiple segments
    for (int i = 0; i < 10; i++) {
        Document doc;
        addTextField(doc, "id", std::to_string(i));
        addTextField(doc, "content", "document " + std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    // Verify we have multiple segments
    const auto& segmentInfos = writer.getSegmentInfos();
    EXPECT_GT(segmentInfos.size(), 1);

    // Delete document that should exist in first segment
    Term term("id", "1");
    writer.deleteDocuments(term);
    writer.commit();

    // Verify deletion
    auto reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 9);
    EXPECT_EQ(reader->maxDoc(), 10);

    reader->decRef();
    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, LiveDocsFileCreation) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    config.setMaxBufferedDocs(5);
    IndexWriter writer(*directory, config);

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        addTextField(doc, "id", std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    // Get segment name
    const auto& segmentInfos = writer.getSegmentInfos();
    ASSERT_GT(segmentInfos.size(), 0);
    std::string segmentName = segmentInfos.info(0)->name();

    // Check .liv file doesn't exist yet
    auto files = directory->listAll();
    bool livFileExists = false;
    for (const auto& file : files) {
        if (file.find(".liv") != std::string::npos) {
            livFileExists = true;
            break;
        }
    }
    EXPECT_FALSE(livFileExists);

    // Delete a document
    Term term("id", "2");
    writer.deleteDocuments(term);
    writer.commit();

    // Check .liv file now exists
    files = directory->listAll();
    livFileExists = false;
    for (const auto& file : files) {
        if (file.find(".liv") != std::string::npos) {
            livFileExists = true;
            break;
        }
    }
    EXPECT_TRUE(livFileExists);

    // Verify segment info has correct delCount
    EXPECT_EQ(segmentInfos.info(0)->delCount(), 1);
    EXPECT_TRUE(segmentInfos.info(0)->hasDeletions());

    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, DeleteNonExistentTerm) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    IndexWriter writer(*directory, config);

    // Add documents
    for (int i = 0; i < 5; i++) {
        Document doc;
        addTextField(doc, "id", std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    // Delete non-existent term
    Term term("id", "999");
    writer.deleteDocuments(term);
    writer.commit();

    // Verify nothing was deleted
    auto reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 5);
    EXPECT_EQ(reader->maxDoc(), 5);
    EXPECT_FALSE(reader->hasDeletions());

    reader->decRef();
    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, DeleteAllDocuments) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    IndexWriter writer(*directory, config);

    // Add documents with same term
    for (int i = 0; i < 5; i++) {
        Document doc;
        addTextField(doc, "type", "deleteme");
        addTextField(doc, "id", std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    // Delete all documents
    Term term("type", "deleteme");
    writer.deleteDocuments(term);
    writer.commit();

    // Verify all deleted
    auto reader = DirectoryReader::open(*directory);
    EXPECT_EQ(reader->numDocs(), 0);
    EXPECT_EQ(reader->maxDoc(), 5);
    EXPECT_TRUE(reader->hasDeletions());

    reader->decRef();
    writer.close();
    removeDir(tempDir);
}

TEST(DeletionIntegrationTest, IncrementalDeletes) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create index writer
    IndexWriterConfig config;
    IndexWriter writer(*directory, config);

    // Add documents
    for (int i = 0; i < 10; i++) {
        Document doc;
        addTextField(doc, "id", std::to_string(i));
        writer.addDocument(doc);
    }
    writer.commit();

    // Delete documents incrementally
    for (int i = 0; i < 5; i++) {
        Term term("id", std::to_string(i));
        writer.deleteDocuments(term);
        writer.commit();

        // Verify progressive deletion
        auto reader = DirectoryReader::open(*directory);
        EXPECT_EQ(reader->numDocs(), 10 - (i + 1));
        EXPECT_EQ(reader->maxDoc(), 10);
        reader->decRef();
    }

    writer.close();
    removeDir(tempDir);
}
