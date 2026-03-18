// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include <gtest/gtest.h>

#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsFormat.h"
#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsReader.h"
#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/FSDirectory.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using namespace diagon;
using namespace diagon::codecs::lucene90;

namespace {

// Helper: create a temp directory
std::string createTempDir(const std::string& prefix) {
    auto path = fs::temp_directory_path() / (prefix + "_XXXXXX");
    std::string pathStr = path.string();
    // Create directory
    fs::create_directories(pathStr);
    return pathStr;
}

// Helper: build FieldInfos with test fields
index::FieldInfos makeFieldInfos() {
    std::vector<index::FieldInfo> fields;

    index::FieldInfo title("title", 0);
    title.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    fields.push_back(title);

    index::FieldInfo body("body", 1);
    body.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    fields.push_back(body);

    index::FieldInfo count("count", 2);
    count.indexOptions = index::IndexOptions::NONE;
    fields.push_back(count);

    index::FieldInfo timestamp("timestamp", 3);
    timestamp.indexOptions = index::IndexOptions::NONE;
    fields.push_back(timestamp);

    return index::FieldInfos(std::move(fields));
}

// Helper: generate a 16-byte segment ID
void makeSegmentID(uint8_t* id) {
    for (int i = 0; i < 16; i++) {
        id[i] = static_cast<uint8_t>(0x10 + i);
    }
}

}  // namespace

class Lucene90StoredFieldsTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = createTempDir("lucene90_stored_fields_test");
        fieldInfos_ = makeFieldInfos();
        makeSegmentID(segmentID_);
    }

    void TearDown() override { fs::remove_all(tempDir_); }

    std::string tempDir_;
    index::FieldInfos fieldInfos_ = makeFieldInfos();
    uint8_t segmentID_[16];
};

TEST_F(Lucene90StoredFieldsTest, BasicRoundTrip) {
    const std::string segmentName = "_0";
    auto dir = store::FSDirectory::open(tempDir_);

    // Write 3 documents with stored fields
    {
        Lucene90OSStoredFieldsFormat format;
        auto writer = format.fieldsWriter(*dir, segmentName, segmentID_);

        // Doc 0: title + body + count
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(0), std::string("Hello World"));
        writer->writeField(*fieldInfos_.fieldInfo(1), std::string("This is the body text."));
        writer->writeField(*fieldInfos_.fieldInfo(2), int32_t(42));
        writer->finishDocument();

        // Doc 1: title + timestamp
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(0), std::string("Second Document"));
        writer->writeField(*fieldInfos_.fieldInfo(3), int64_t(1700000000000LL));
        writer->finishDocument();

        // Doc 2: body only
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(1), std::string("Just a body field."));
        writer->finishDocument();

        writer->finish(3);
        writer->close();
    }

    // Verify files were created
    ASSERT_TRUE(fs::exists(tempDir_ + "/_0.fdt"));
    ASSERT_TRUE(fs::exists(tempDir_ + "/_0.fdx"));
    ASSERT_TRUE(fs::exists(tempDir_ + "/_0.fdm"));

    // Read back
    {
        Lucene90OSStoredFieldsFormat format;
        auto reader = format.fieldsReader(*dir, segmentName, segmentID_, fieldInfos_);

        ASSERT_EQ(reader->numDocs(), 3);

        // Doc 0
        auto doc0 = reader->document(0);
        ASSERT_EQ(doc0.size(), 3u);
        EXPECT_EQ(std::get<std::string>(doc0["title"]), "Hello World");
        EXPECT_EQ(std::get<std::string>(doc0["body"]), "This is the body text.");
        EXPECT_EQ(std::get<int32_t>(doc0["count"]), 42);

        // Doc 1
        auto doc1 = reader->document(1);
        ASSERT_EQ(doc1.size(), 2u);
        EXPECT_EQ(std::get<std::string>(doc1["title"]), "Second Document");
        EXPECT_EQ(std::get<int64_t>(doc1["timestamp"]), 1700000000000LL);

        // Doc 2
        auto doc2 = reader->document(2);
        ASSERT_EQ(doc2.size(), 1u);
        EXPECT_EQ(std::get<std::string>(doc2["body"]), "Just a body field.");

        reader->close();
    }
}

TEST_F(Lucene90StoredFieldsTest, MultiChunkRoundTrip) {
    const std::string segmentName = "_1";
    auto dir = store::FSDirectory::open(tempDir_);

    // Write enough documents to trigger multiple chunks (>1024 docs)
    const int numDocs = 2048;

    {
        Lucene90OSStoredFieldsFormat format;
        auto writer = format.fieldsWriter(*dir, segmentName, segmentID_);

        for (int i = 0; i < numDocs; i++) {
            writer->startDocument();
            writer->writeField(*fieldInfos_.fieldInfo(0),
                               std::string("Document #" + std::to_string(i)));
            writer->writeField(*fieldInfos_.fieldInfo(2), int32_t(i * 10));
            writer->finishDocument();
        }

        writer->finish(numDocs);
        writer->close();
    }

    // Read back and verify every document
    {
        Lucene90OSStoredFieldsFormat format;
        auto reader = format.fieldsReader(*dir, segmentName, segmentID_, fieldInfos_);

        ASSERT_EQ(reader->numDocs(), numDocs);

        for (int i = 0; i < numDocs; i++) {
            auto doc = reader->document(i);
            ASSERT_EQ(doc.size(), 2u) << "Doc " << i;
            EXPECT_EQ(std::get<std::string>(doc["title"]),
                       "Document #" + std::to_string(i))
                << "Doc " << i;
            EXPECT_EQ(std::get<int32_t>(doc["count"]), i * 10)
                << "Doc " << i;
        }

        reader->close();
    }
}

TEST_F(Lucene90StoredFieldsTest, SingleDocChunk) {
    const std::string segmentName = "_2";
    auto dir = store::FSDirectory::open(tempDir_);

    // Single document
    {
        Lucene90OSStoredFieldsFormat format;
        auto writer = format.fieldsWriter(*dir, segmentName, segmentID_);

        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(0), std::string("Only document"));
        writer->writeField(*fieldInfos_.fieldInfo(2), int32_t(-999));
        writer->writeField(*fieldInfos_.fieldInfo(3), int64_t(0));
        writer->finishDocument();

        writer->finish(1);
        writer->close();
    }

    // Read back
    {
        Lucene90OSStoredFieldsFormat format;
        auto reader = format.fieldsReader(*dir, segmentName, segmentID_, fieldInfos_);

        ASSERT_EQ(reader->numDocs(), 1);

        auto doc = reader->document(0);
        ASSERT_EQ(doc.size(), 3u);
        EXPECT_EQ(std::get<std::string>(doc["title"]), "Only document");
        EXPECT_EQ(std::get<int32_t>(doc["count"]), -999);
        EXPECT_EQ(std::get<int64_t>(doc["timestamp"]), 0);

        reader->close();
    }
}

TEST_F(Lucene90StoredFieldsTest, EmptyStringField) {
    const std::string segmentName = "_3";
    auto dir = store::FSDirectory::open(tempDir_);

    {
        Lucene90OSStoredFieldsFormat format;
        auto writer = format.fieldsWriter(*dir, segmentName, segmentID_);

        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(0), std::string(""));
        writer->writeField(*fieldInfos_.fieldInfo(1), std::string("non-empty"));
        writer->finishDocument();

        writer->finish(1);
        writer->close();
    }

    {
        Lucene90OSStoredFieldsFormat format;
        auto reader = format.fieldsReader(*dir, segmentName, segmentID_, fieldInfos_);

        auto doc = reader->document(0);
        EXPECT_EQ(std::get<std::string>(doc["title"]), "");
        EXPECT_EQ(std::get<std::string>(doc["body"]), "non-empty");

        reader->close();
    }
}

TEST_F(Lucene90StoredFieldsTest, NumericEdgeCases) {
    const std::string segmentName = "_4";
    auto dir = store::FSDirectory::open(tempDir_);

    {
        Lucene90OSStoredFieldsFormat format;
        auto writer = format.fieldsWriter(*dir, segmentName, segmentID_);

        // Doc 0: max/min int32
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(2), int32_t(2147483647));
        writer->finishDocument();

        // Doc 1: min int32
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(2), int32_t(-2147483648));
        writer->finishDocument();

        // Doc 2: timestamp divisible by DAY
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(3), int64_t(86400000LL * 19000));
        writer->finishDocument();

        // Doc 3: timestamp divisible by HOUR
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(3), int64_t(3600000LL * 500000));
        writer->finishDocument();

        // Doc 4: timestamp divisible by SECOND
        writer->startDocument();
        writer->writeField(*fieldInfos_.fieldInfo(3), int64_t(1000LL * 1700000000));
        writer->finishDocument();

        writer->finish(5);
        writer->close();
    }

    {
        Lucene90OSStoredFieldsFormat format;
        auto reader = format.fieldsReader(*dir, segmentName, segmentID_, fieldInfos_);

        ASSERT_EQ(reader->numDocs(), 5);

        auto doc0 = reader->document(0);
        EXPECT_EQ(std::get<int32_t>(doc0["count"]), 2147483647);

        auto doc1 = reader->document(1);
        EXPECT_EQ(std::get<int32_t>(doc1["count"]), -2147483648);

        auto doc2 = reader->document(2);
        EXPECT_EQ(std::get<int64_t>(doc2["timestamp"]), 86400000LL * 19000);

        auto doc3 = reader->document(3);
        EXPECT_EQ(std::get<int64_t>(doc3["timestamp"]), 3600000LL * 500000);

        auto doc4 = reader->document(4);
        EXPECT_EQ(std::get<int64_t>(doc4["timestamp"]), 1000LL * 1700000000);

        reader->close();
    }
}
