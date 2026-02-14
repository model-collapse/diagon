// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsReader.h"

#include "diagon/codecs/StoredFieldsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <variant>

using namespace diagon::codecs;
using namespace diagon::index;
using namespace diagon::store;

// Helper function to create a temporary directory
static std::string createTempDir() {
    std::filesystem::path tempPath = std::filesystem::temp_directory_path();
    tempPath /= "diagon_stored_fields_test_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(tempPath);
    return tempPath.string();
}

// Helper function to remove directory and all contents
static void removeDir(const std::string& path) {
    std::filesystem::remove_all(path);
}

// ==================== StoredFieldsReader Tests ====================

TEST(StoredFieldsReaderTest, BasicReadWrite) {
    // Create temporary directory
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    // Create field infos
    FieldInfosBuilder builder;
    builder.getOrAdd("title");
    builder.getOrAdd("count");

    std::string segmentName = "_test";

    // Write documents
    {
        StoredFieldsWriter writer(segmentName);

        FieldInfo* titleField = builder.getFieldInfo("title");
        FieldInfo* countField = builder.getFieldInfo("count");

        // Write first document
        writer.startDocument();
        writer.writeField(*titleField, std::string("Test Document"));
        writer.writeField(*countField, int64_t(42));
        writer.finishDocument();

        // Write second document
        writer.startDocument();
        writer.writeField(*titleField, std::string("Another Document"));
        writer.writeField(*countField, int64_t(100));
        writer.finishDocument();

        writer.finish(2);

        // Flush to disk
        auto dataOut = directory->createOutput(segmentName + ".fdt", IOContext::DEFAULT);
        auto indexOut = directory->createOutput(segmentName + ".fdx", IOContext::DEFAULT);
        writer.flush(*dataOut, *indexOut);
        dataOut->close();
        indexOut->close();

        writer.close();
    }

    // Finish field infos after writing
    auto fieldInfos = builder.finish();

    // Read documents
    {
        StoredFieldsReader reader(directory.get(), segmentName, *fieldInfos);

        EXPECT_EQ(reader.numDocs(), 2);

        // Read first document
        auto doc0 = reader.document(0);
        EXPECT_EQ(doc0.size(), 2);
        EXPECT_EQ(std::get<std::string>(doc0["title"]), "Test Document");
        EXPECT_EQ(std::get<int64_t>(doc0["count"]), 42);

        // Read second document
        auto doc1 = reader.document(1);
        EXPECT_EQ(doc1.size(), 2);
        EXPECT_EQ(std::get<std::string>(doc1["title"]), "Another Document");
        EXPECT_EQ(std::get<int64_t>(doc1["count"]), 100);

        reader.close();
    }

    // Clean up
    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsReaderTest, MultipleFieldTypes) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    FieldInfosBuilder builder;
    builder.getOrAdd("name");
    builder.getOrAdd("age");
    builder.getOrAdd("score");

    std::string segmentName = "_test";

    // Write document
    {
        StoredFieldsWriter writer(segmentName);

        FieldInfo* nameField = builder.getFieldInfo("name");
        FieldInfo* ageField = builder.getFieldInfo("age");
        FieldInfo* scoreField = builder.getFieldInfo("score");

        writer.startDocument();
        writer.writeField(*nameField, std::string("John Doe"));
        writer.writeField(*ageField, int32_t(30));
        writer.writeField(*scoreField, int64_t(9500));
        writer.finishDocument();

        writer.finish(1);

        auto dataOut = directory->createOutput(segmentName + ".fdt", IOContext::DEFAULT);
        auto indexOut = directory->createOutput(segmentName + ".fdx", IOContext::DEFAULT);
        writer.flush(*dataOut, *indexOut);
        dataOut->close();
        indexOut->close();

        writer.close();
    }

    // Finish field infos after writing
    auto fieldInfos = builder.finish();

    // Read document
    {
        StoredFieldsReader reader(directory.get(), segmentName, *fieldInfos);

        auto doc = reader.document(0);
        EXPECT_EQ(doc.size(), 3);
        EXPECT_EQ(std::get<std::string>(doc["name"]), "John Doe");
        EXPECT_EQ(std::get<int32_t>(doc["age"]), 30);
        EXPECT_EQ(std::get<int64_t>(doc["score"]), 9500);

        reader.close();
    }

    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsReaderTest, EmptyDocument) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    FieldInfosBuilder builder;

    std::string segmentName = "_test";

    // Write empty document
    {
        StoredFieldsWriter writer(segmentName);
        writer.startDocument();
        writer.finishDocument();
        writer.finish(1);

        auto dataOut = directory->createOutput(segmentName + ".fdt", IOContext::DEFAULT);
        auto indexOut = directory->createOutput(segmentName + ".fdx", IOContext::DEFAULT);
        writer.flush(*dataOut, *indexOut);
        dataOut->close();
        indexOut->close();

        writer.close();
    }

    // Finish field infos after writing
    auto fieldInfos = builder.finish();

    // Read empty document
    {
        StoredFieldsReader reader(directory.get(), segmentName, *fieldInfos);

        auto doc = reader.document(0);
        EXPECT_EQ(doc.size(), 0);

        reader.close();
    }

    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsReaderTest, OutOfRangeDocID) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    FieldInfosBuilder builder;
    builder.getOrAdd("test");

    std::string segmentName = "_test";

    // Write one document
    {
        StoredFieldsWriter writer(segmentName);

        FieldInfo* testField = builder.getFieldInfo("test");

        writer.startDocument();
        writer.writeField(*testField, std::string("value"));
        writer.finishDocument();

        writer.finish(1);

        auto dataOut = directory->createOutput(segmentName + ".fdt", IOContext::DEFAULT);
        auto indexOut = directory->createOutput(segmentName + ".fdx", IOContext::DEFAULT);
        writer.flush(*dataOut, *indexOut);
        dataOut->close();
        indexOut->close();

        writer.close();
    }

    // Finish field infos after writing
    auto fieldInfos = builder.finish();

    // Try to read out-of-range documents
    {
        StoredFieldsReader reader(directory.get(), segmentName, *fieldInfos);

        EXPECT_THROW(reader.document(-1), std::runtime_error);
        EXPECT_THROW(reader.document(1), std::runtime_error);
        EXPECT_THROW(reader.document(100), std::runtime_error);

        reader.close();
    }

    directory->close();
    removeDir(tempDir);
}

TEST(StoredFieldsReaderTest, RandomAccessMultipleDocs) {
    std::string tempDir = createTempDir();
    auto directory = std::make_unique<FSDirectory>(tempDir);

    FieldInfosBuilder builder;
    builder.getOrAdd("id");
    builder.getOrAdd("value");

    std::string segmentName = "_test";

    // Write 10 documents
    {
        StoredFieldsWriter writer(segmentName);

        FieldInfo* idField = builder.getFieldInfo("id");
        FieldInfo* valueField = builder.getFieldInfo("value");

        for (int i = 0; i < 10; i++) {
            writer.startDocument();
            writer.writeField(*idField, int32_t(i));
            writer.writeField(*valueField, std::string("Document " + std::to_string(i)));
            writer.finishDocument();
        }

        writer.finish(10);

        auto dataOut = directory->createOutput(segmentName + ".fdt", IOContext::DEFAULT);
        auto indexOut = directory->createOutput(segmentName + ".fdx", IOContext::DEFAULT);
        writer.flush(*dataOut, *indexOut);
        dataOut->close();
        indexOut->close();

        writer.close();
    }

    // Finish field infos after writing
    auto fieldInfos = builder.finish();

    // Read documents in random order
    {
        StoredFieldsReader reader(directory.get(), segmentName, *fieldInfos);

        EXPECT_EQ(reader.numDocs(), 10);

        // Read in order: 5, 2, 8, 0, 9
        auto doc5 = reader.document(5);
        EXPECT_EQ(std::get<int32_t>(doc5["id"]), 5);
        EXPECT_EQ(std::get<std::string>(doc5["value"]), "Document 5");

        auto doc2 = reader.document(2);
        EXPECT_EQ(std::get<int32_t>(doc2["id"]), 2);
        EXPECT_EQ(std::get<std::string>(doc2["value"]), "Document 2");

        auto doc8 = reader.document(8);
        EXPECT_EQ(std::get<int32_t>(doc8["id"]), 8);
        EXPECT_EQ(std::get<std::string>(doc8["value"]), "Document 8");

        auto doc0 = reader.document(0);
        EXPECT_EQ(std::get<int32_t>(doc0["id"]), 0);
        EXPECT_EQ(std::get<std::string>(doc0["value"]), "Document 0");

        auto doc9 = reader.document(9);
        EXPECT_EQ(std::get<int32_t>(doc9["id"]), 9);
        EXPECT_EQ(std::get<std::string>(doc9["value"]), "Document 9");

        reader.close();
    }

    directory->close();
    removeDir(tempDir);
}
