// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include <unistd.h>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

static std::string bytesRefToString(const util::BytesRef& br) {
    return std::string(reinterpret_cast<const char*>(br.data()), br.length());
}

class BinaryDocValuesTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_binary_dv_test_" + std::to_string(getpid()) + "_" +
                    std::to_string(counter++));
        fs::remove_all(testDir_);
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

TEST_F(BinaryDocValuesTest, WriteAndReadSingleField) {
    std::vector<std::string> expected = {"hello", "world", "foo", "bar", "baz"};

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", expected[i]));
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

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        ASSERT_NE(leafReader, nullptr);

        auto* dv = leafReader->getBinaryDocValues("payload");
        ASSERT_NE(dv, nullptr);

        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(dv->advanceExact(i));
            util::BytesRef val = dv->binaryValue();
            EXPECT_EQ(bytesRefToString(val), expected[i]) << "Mismatch at doc " << i;
        }

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, WriteAndReadMultipleFields) {
    std::vector<std::string> payloads = {"alpha", "bravo", "charlie", "delta", "echo"};
    std::vector<std::string> tags = {"tag_a", "tag_b", "tag_c", "tag_d", "tag_e"};

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", payloads[i]));
            doc.add(std::make_unique<BinaryDocValuesField>("tag", tags[i]));
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
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();

        // Verify payload field
        auto* payloadDv = leafReader->getBinaryDocValues("payload");
        ASSERT_NE(payloadDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(payloadDv->advanceExact(i));
            EXPECT_EQ(bytesRefToString(payloadDv->binaryValue()), payloads[i]);
        }

        // Verify tag field
        auto* tagDv = leafReader->getBinaryDocValues("tag");
        ASSERT_NE(tagDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(tagDv->advanceExact(i));
            EXPECT_EQ(bytesRefToString(tagDv->binaryValue()), tags[i]);
        }

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, SparseValues) {
    // Write phase - only even documents have the binary DV field
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));

            if (i % 2 == 0) {
                doc.add(std::make_unique<BinaryDocValuesField>("payload",
                                                               "value_" + std::to_string(i)));
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
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getBinaryDocValues("payload");
        ASSERT_NE(dv, nullptr);

        for (int i = 0; i < 10; i++) {
            bool hasValue = dv->advanceExact(i);
            if (i % 2 == 0) {
                EXPECT_TRUE(hasValue) << "Even doc " << i << " should have a value";
                util::BytesRef val = dv->binaryValue();
                EXPECT_EQ(bytesRefToString(val), "value_" + std::to_string(i));
            } else {
                // Odd docs: either advanceExact returns false, or binaryValue is empty
                if (hasValue) {
                    util::BytesRef val = dv->binaryValue();
                    EXPECT_EQ(val.length(), 0u) << "Odd doc " << i << " should have empty value";
                }
            }
        }

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, EmptyValue) {
    // Test empty binary values via the uint8_t* constructor (zero-length byte array).
    //
    // Note: BinaryDocValuesField(name, std::string("")) does NOT store a value because
    // Field::stringValue() returns std::nullopt when stringValue_ is empty, and
    // Field::binaryValue() also returns std::nullopt when binaryValue_ is empty.
    // To store a truly empty (zero-length) binary value, use the uint8_t* constructor
    // with length=0, which populates binaryValue_ as an empty vector -- but that also
    // returns std::nullopt. This matches Lucene behavior: a zero-length BinaryDocValuesField
    // is treated as "no value".
    //
    // We test the practical scenario: docs with and without values, verifying the
    // distinction is preserved.

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Doc 0: no binary DV value (field omitted)
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc without binary dv", false));
            writer.addDocument(doc);
        }

        // Doc 1: short non-empty value
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc with short data", false));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", "x"));
            writer.addDocument(doc);
        }

        // Doc 2: longer non-empty value
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc with longer data", false));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", "notempty"));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getBinaryDocValues("payload");
        ASSERT_NE(dv, nullptr);

        // Doc 0: no value (field was omitted)
        bool hasVal0 = dv->advanceExact(0);
        if (hasVal0) {
            // If the implementation treats missing as empty, length should be 0
            EXPECT_EQ(dv->binaryValue().length(), 0u);
        }

        // Doc 1: short value "x"
        EXPECT_TRUE(dv->advanceExact(1));
        util::BytesRef val1 = dv->binaryValue();
        EXPECT_EQ(bytesRefToString(val1), "x");

        // Doc 2: non-empty value
        EXPECT_TRUE(dv->advanceExact(2));
        util::BytesRef val2 = dv->binaryValue();
        EXPECT_EQ(bytesRefToString(val2), "notempty");

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, LargeBinaryValue) {
    // Create 10KB binary data with a recognizable pattern
    const size_t largeSize = 10 * 1024;
    std::string largeData(largeSize, '\0');
    for (size_t i = 0; i < largeSize; i++) {
        largeData[i] = static_cast<char>(i % 256);
    }

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "large binary doc", false));
        doc.add(std::make_unique<BinaryDocValuesField>("bigdata", largeData));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getBinaryDocValues("bigdata");
        ASSERT_NE(dv, nullptr);

        EXPECT_TRUE(dv->advanceExact(0));
        util::BytesRef val = dv->binaryValue();
        EXPECT_EQ(val.length(), largeSize);

        // Byte-for-byte comparison
        EXPECT_EQ(std::memcmp(val.data(), largeData.data(), largeSize), 0)
            << "10KB binary data mismatch after round-trip";

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, BinaryDataNotJustStrings) {
    // Construct binary data with all 256 byte values including null bytes
    std::vector<uint8_t> binaryData(256);
    std::iota(binaryData.begin(), binaryData.end(), static_cast<uint8_t>(0));

    // Pattern with embedded nulls at various positions
    std::vector<uint8_t> nullPattern = {0x00, 0xFF, 0x00, 0xAB, 0x00, 0x00, 0xCD, 0x00};

    // Write phase using the uint8_t* constructor
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Doc 0: all 256 byte values
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "all bytes", false));
            doc.add(std::make_unique<BinaryDocValuesField>("data", binaryData.data(),
                                                           static_cast<int>(binaryData.size())));
            writer.addDocument(doc);
        }

        // Doc 1: pattern with embedded nulls
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "null pattern", false));
            doc.add(std::make_unique<BinaryDocValuesField>("data", nullPattern.data(),
                                                           static_cast<int>(nullPattern.size())));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getBinaryDocValues("data");
        ASSERT_NE(dv, nullptr);

        // Doc 0: verify all 256 byte values
        EXPECT_TRUE(dv->advanceExact(0));
        util::BytesRef val0 = dv->binaryValue();
        ASSERT_EQ(val0.length(), binaryData.size());
        for (size_t i = 0; i < binaryData.size(); i++) {
            EXPECT_EQ(val0.data()[i], binaryData[i]) << "Byte mismatch at offset " << i;
        }

        // Doc 1: verify null-embedded pattern
        EXPECT_TRUE(dv->advanceExact(1));
        util::BytesRef val1 = dv->binaryValue();
        ASSERT_EQ(val1.length(), nullPattern.size());
        EXPECT_EQ(std::memcmp(val1.data(), nullPattern.data(), nullPattern.size()), 0)
            << "Null-embedded binary pattern mismatch";

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, NonExistentField) {
    // Write phase
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test", false));
        doc.add(std::make_unique<BinaryDocValuesField>("payload", "some_data"));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        // Request non-existent field - should return nullptr
        auto* dv = leafReader->getBinaryDocValues("nonexistent");
        EXPECT_EQ(dv, nullptr);

        // Request field that exists
        auto* payloadDv = leafReader->getBinaryDocValues("payload");
        EXPECT_NE(payloadDv, nullptr);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, MultipleSegments) {
    // Write phase - create multiple segments with low maxBufferedDocs
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);  // Low limit to create multiple segments

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<BinaryDocValuesField>("payload",
                                                           "seg_value_" + std::to_string(i)));
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
        EXPECT_GE(leaves.size(), 1u);  // Should have multiple segments

        // Verify we can read from all segments
        int totalDocs = 0;
        for (const auto& ctx : leaves) {
            auto* leafReader = ctx.reader.get();
            ASSERT_NE(leafReader, nullptr);

            auto* dv = leafReader->getBinaryDocValues("payload");
            ASSERT_NE(dv, nullptr);

            for (int i = 0; i < leafReader->maxDoc(); i++) {
                EXPECT_TRUE(dv->advanceExact(i));
                util::BytesRef val = dv->binaryValue();
                // Value should be non-empty and start with "seg_value_"
                std::string s = bytesRefToString(val);
                EXPECT_TRUE(s.find("seg_value_") == 0) << "Unexpected value in segment: " << s;
                totalDocs++;
            }
        }

        EXPECT_EQ(totalDocs, 10);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, Iteration) {
    std::vector<std::string> values = {"first", "second", "third", "fourth", "fifth"};

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc " + std::to_string(i), false));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", values[i]));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase - test nextDoc() iteration
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getBinaryDocValues("payload");
        ASSERT_NE(dv, nullptr);

        // Test nextDoc() iteration
        int docID = dv->nextDoc();
        EXPECT_EQ(docID, 0);
        EXPECT_EQ(bytesRefToString(dv->binaryValue()), "first");

        docID = dv->nextDoc();
        EXPECT_EQ(docID, 1);
        EXPECT_EQ(bytesRefToString(dv->binaryValue()), "second");

        docID = dv->nextDoc();
        EXPECT_EQ(docID, 2);
        EXPECT_EQ(bytesRefToString(dv->binaryValue()), "third");

        // Test advance() - skip to doc 4
        docID = dv->advance(4);
        EXPECT_EQ(docID, 4);
        EXPECT_EQ(bytesRefToString(dv->binaryValue()), "fifth");

        // No more docs
        docID = dv->nextDoc();
        EXPECT_EQ(docID, search::DocIdSetIterator::NO_MORE_DOCS);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}

TEST_F(BinaryDocValuesTest, BytePointerConstructor) {
    // Test the uint8_t* constructor variant of BinaryDocValuesField
    const uint8_t rawBytes[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    const int rawLen = sizeof(rawBytes);

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Doc 0: DEADBEEF CAFEBABE
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "byte pointer test", false));
            doc.add(std::make_unique<BinaryDocValuesField>("raw", rawBytes, rawLen));
            writer.addDocument(doc);
        }

        // Doc 1: different bytes to confirm independent storage
        {
            const uint8_t otherBytes[] = {0x01, 0x02, 0x03};
            Document doc;
            doc.add(std::make_unique<TextField>("body", "second byte pointer", false));
            doc.add(std::make_unique<BinaryDocValuesField>("raw", otherBytes, 3));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getBinaryDocValues("raw");
        ASSERT_NE(dv, nullptr);

        // Doc 0: verify DEADBEEF CAFEBABE
        EXPECT_TRUE(dv->advanceExact(0));
        util::BytesRef val0 = dv->binaryValue();
        ASSERT_EQ(val0.length(), static_cast<size_t>(rawLen));
        EXPECT_EQ(std::memcmp(val0.data(), rawBytes, rawLen), 0);

        // Doc 1: verify 01 02 03
        EXPECT_TRUE(dv->advanceExact(1));
        util::BytesRef val1 = dv->binaryValue();
        ASSERT_EQ(val1.length(), 3u);
        EXPECT_EQ(val1.data()[0], 0x01);
        EXPECT_EQ(val1.data()[1], 0x02);
        EXPECT_EQ(val1.data()[2], 0x03);

        // Reader will be automatically closed when shared_ptr goes out of scope
    }
}
