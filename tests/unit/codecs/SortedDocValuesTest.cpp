// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
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

class SortedDocValuesTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_sorted_dv_test_" + std::to_string(getpid()) + "_" +
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

TEST_F(SortedDocValuesTest, WriteAndReadSingleField) {
    fs::path subDir = testDir_ / "single_field";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*subFsDir, config);

        // doc 0: "apple"
        // doc 1: "banana"
        // doc 2: "cherry"
        // doc 3: "apple"
        // doc 4: "date"
        std::vector<std::string> values = {"apple", "banana", "cherry", "apple", "date"};

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedDocValuesField>("fruit", values[i]));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 5);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1);

        auto* leafReader = leaves[0].reader.get();
        ASSERT_NE(leafReader, nullptr);

        auto* dv = leafReader->getSortedDocValues("fruit");
        ASSERT_NE(dv, nullptr);

        // Unique sorted values: apple(0), banana(1), cherry(2), date(3)
        EXPECT_EQ(dv->getValueCount(), 4);

        // doc 0 -> "apple"
        ASSERT_TRUE(dv->advanceExact(0));
        int ord0 = dv->ordValue();
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(ord0)), "apple");

        // doc 1 -> "banana"
        ASSERT_TRUE(dv->advanceExact(1));
        int ord1 = dv->ordValue();
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(ord1)), "banana");

        // doc 2 -> "cherry"
        ASSERT_TRUE(dv->advanceExact(2));
        int ord2 = dv->ordValue();
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(ord2)), "cherry");

        // doc 3 -> "apple" (same ordinal as doc 0)
        ASSERT_TRUE(dv->advanceExact(3));
        EXPECT_EQ(dv->ordValue(), ord0);
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(dv->ordValue())), "apple");

        // doc 4 -> "date"
        ASSERT_TRUE(dv->advanceExact(4));
        int ord4 = dv->ordValue();
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(ord4)), "date");

        // Verify ordinals are assigned in lexicographic order:
        // apple < banana < cherry < date
        EXPECT_LT(ord0, ord1);
        EXPECT_LT(ord1, ord2);
        EXPECT_LT(ord2, ord4);
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, WriteAndReadMultipleFields) {
    fs::path subDir = testDir_ / "multiple_fields";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*subFsDir, config);

        std::vector<std::string> categories = {"electronics", "books", "electronics", "clothing",
                                               "books"};
        std::vector<std::string> statuses = {"active", "inactive", "active", "active", "pending"};

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedDocValuesField>("category", categories[i]));
            doc.add(std::make_unique<SortedDocValuesField>("status", statuses[i]));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        ASSERT_NE(reader, nullptr);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1);

        auto* leafReader = leaves[0].reader.get();

        // Verify category field
        auto* catDv = leafReader->getSortedDocValues("category");
        ASSERT_NE(catDv, nullptr);
        // Unique categories: books, clothing, electronics (3)
        EXPECT_EQ(catDv->getValueCount(), 3);

        ASSERT_TRUE(catDv->advanceExact(0));
        EXPECT_EQ(bytesRefToString(catDv->lookupOrd(catDv->ordValue())), "electronics");

        ASSERT_TRUE(catDv->advanceExact(1));
        EXPECT_EQ(bytesRefToString(catDv->lookupOrd(catDv->ordValue())), "books");

        ASSERT_TRUE(catDv->advanceExact(3));
        EXPECT_EQ(bytesRefToString(catDv->lookupOrd(catDv->ordValue())), "clothing");

        // Verify status field independently
        auto* statusDv = leafReader->getSortedDocValues("status");
        ASSERT_NE(statusDv, nullptr);
        // Unique statuses: active, inactive, pending (3)
        EXPECT_EQ(statusDv->getValueCount(), 3);

        ASSERT_TRUE(statusDv->advanceExact(0));
        EXPECT_EQ(bytesRefToString(statusDv->lookupOrd(statusDv->ordValue())), "active");

        ASSERT_TRUE(statusDv->advanceExact(1));
        EXPECT_EQ(bytesRefToString(statusDv->lookupOrd(statusDv->ordValue())), "inactive");

        ASSERT_TRUE(statusDv->advanceExact(4));
        EXPECT_EQ(bytesRefToString(statusDv->lookupOrd(statusDv->ordValue())), "pending");
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, SparseValues) {
    fs::path subDir = testDir_ / "sparse";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase - only even docs have sorted DV
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*subFsDir, config);

        std::vector<std::string> evenValues = {"alpha", "bravo", "charlie", "delta", "echo"};

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));

            if (i % 2 == 0) {
                doc.add(std::make_unique<SortedDocValuesField>("tag", evenValues[i / 2]));
            }

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedDocValues("tag");
        ASSERT_NE(dv, nullptr);

        std::vector<std::string> expectedValues = {"alpha", "bravo", "charlie", "delta", "echo"};

        for (int i = 0; i < 10; i++) {
            bool hasValue = dv->advanceExact(i);
            if (i % 2 == 0) {
                // Even docs have values
                ASSERT_TRUE(hasValue) << "doc " << i << " should have a value";
                int ord = dv->ordValue();
                EXPECT_GE(ord, 0) << "doc " << i << " ordinal should be >= 0";
                std::string actual = bytesRefToString(dv->lookupOrd(ord));
                EXPECT_EQ(actual, expectedValues[i / 2]) << "doc " << i << " value mismatch";
            } else {
                // Odd docs should NOT have values (advanceExact returns false)
                EXPECT_FALSE(hasValue) << "doc " << i << " should NOT have a value";
                // After advanceExact returns false, ordValue() returns -1
                EXPECT_EQ(dv->ordValue(), -1)
                    << "doc " << i << " ordinal should be -1 for missing value";
            }
        }
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, UniqueValues) {
    fs::path subDir = testDir_ / "unique";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase - all docs have different values
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*subFsDir, config);

        std::vector<std::string> values = {"delta",   "alpha",   "echo", "bravo",
                                           "charlie", "foxtrot", "golf"};

        for (size_t i = 0; i < values.size(); i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc " + std::to_string(i), false));
            doc.add(std::make_unique<SortedDocValuesField>("code", values[i]));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedDocValues("code");
        ASSERT_NE(dv, nullptr);

        // All 7 values are unique
        EXPECT_EQ(dv->getValueCount(), 7);

        // Verify ordinals are in lexicographic order
        for (int ord = 0; ord < dv->getValueCount() - 1; ord++) {
            std::string a = bytesRefToString(dv->lookupOrd(ord));
            std::string b = bytesRefToString(dv->lookupOrd(ord + 1));
            EXPECT_LT(a, b) << "Ordinals not in sorted order: ord " << ord << " = \"" << a
                            << "\", ord " << (ord + 1) << " = \"" << b << "\"";
        }

        // Verify each doc maps back to its original value
        std::vector<std::string> values = {"delta",   "alpha",   "echo", "bravo",
                                           "charlie", "foxtrot", "golf"};
        for (size_t i = 0; i < values.size(); i++) {
            ASSERT_TRUE(dv->advanceExact(static_cast<int>(i)));
            std::string actual = bytesRefToString(dv->lookupOrd(dv->ordValue()));
            EXPECT_EQ(actual, values[i]) << "doc " << i << " value mismatch";
        }
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, AllSameValue) {
    fs::path subDir = testDir_ / "all_same";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase - all 10 docs have the same value
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*subFsDir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedDocValuesField>("status", "constant_value"));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedDocValues("status");
        ASSERT_NE(dv, nullptr);

        // Only 1 unique value
        EXPECT_EQ(dv->getValueCount(), 1);

        for (int i = 0; i < 10; i++) {
            ASSERT_TRUE(dv->advanceExact(i));
            EXPECT_EQ(dv->ordValue(), 0) << "doc " << i << " should have ordinal 0";
            EXPECT_EQ(bytesRefToString(dv->lookupOrd(0)), "constant_value");
        }
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, EmptyStringValue) {
    fs::path subDir = testDir_ / "empty_string";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Note: Field::stringValue() returns std::nullopt for empty strings,
    // so SortedDocValuesField("") is treated as missing at the ingestion layer.
    // This test verifies that behavior: empty string fields are treated as
    // absent, while a single-space string " " is a valid non-empty value.

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*subFsDir, config);

        // doc 0: empty string -> treated as missing
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc zero", false));
            doc.add(std::make_unique<SortedDocValuesField>("label", ""));
            writer.addDocument(doc);
        }
        // doc 1: "hello" -> valid value
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc one", false));
            doc.add(std::make_unique<SortedDocValuesField>("label", "hello"));
            writer.addDocument(doc);
        }
        // doc 2: " " (single space) -> valid non-empty value
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc two", false));
            doc.add(std::make_unique<SortedDocValuesField>("label", " "));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedDocValues("label");
        ASSERT_NE(dv, nullptr);

        // Only 2 unique values: " " and "hello" (empty string is missing)
        EXPECT_EQ(dv->getValueCount(), 2);

        // doc 0: empty string was treated as missing
        EXPECT_FALSE(dv->advanceExact(0))
            << "Empty string SortedDocValuesField is treated as missing";

        // doc 1: "hello"
        ASSERT_TRUE(dv->advanceExact(1));
        int ordHello = dv->ordValue();
        std::string val1 = bytesRefToString(dv->lookupOrd(ordHello));
        EXPECT_EQ(val1, "hello");

        // doc 2: " " (single space, valid value)
        ASSERT_TRUE(dv->advanceExact(2));
        int ordSpace = dv->ordValue();
        std::string val2 = bytesRefToString(dv->lookupOrd(ordSpace));
        EXPECT_EQ(val2, " ");

        // " " sorts before "hello" lexicographically
        EXPECT_LT(ordSpace, ordHello);
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, LongStringValue) {
    fs::path subDir = testDir_ / "long_string";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Create a 1200-character string
    std::string longValue(1200, 'x');
    for (size_t i = 0; i < longValue.size(); i++) {
        longValue[i] = 'a' + static_cast<char>(i % 26);
    }

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*subFsDir, config);

        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc with long value", false));
            doc.add(std::make_unique<SortedDocValuesField>("data", longValue));
            writer.addDocument(doc);
        }
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc with short value", false));
            doc.add(std::make_unique<SortedDocValuesField>("data", "short"));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedDocValues("data");
        ASSERT_NE(dv, nullptr);

        EXPECT_EQ(dv->getValueCount(), 2);

        // doc 0: long string
        ASSERT_TRUE(dv->advanceExact(0));
        std::string actual = bytesRefToString(dv->lookupOrd(dv->ordValue()));
        EXPECT_EQ(actual.size(), 1200u);
        EXPECT_EQ(actual, longValue);

        // doc 1: "short"
        ASSERT_TRUE(dv->advanceExact(1));
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(dv->ordValue())), "short");
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, NonExistentField) {
    fs::path subDir = testDir_ / "nonexistent";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase
    {
        IndexWriterConfig config;
        IndexWriter writer(*subFsDir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test document", false));
        doc.add(std::make_unique<SortedDocValuesField>("existing_field", "value"));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        // Request non-existent field - should return nullptr
        auto* dv = leafReader->getSortedDocValues("nonexistent");
        EXPECT_EQ(dv, nullptr);

        // Request field that exists - should work
        auto* existingDv = leafReader->getSortedDocValues("existing_field");
        EXPECT_NE(existingDv, nullptr);
        ASSERT_TRUE(existingDv->advanceExact(0));
        EXPECT_EQ(bytesRefToString(existingDv->lookupOrd(existingDv->ordValue())), "value");
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, MultipleSegments) {
    fs::path subDir = testDir_ / "multi_segment";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase - create multiple segments with maxBufferedDocs=3
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*subFsDir, config);

        std::vector<std::string> values = {"alpha",   "bravo", "charlie", "delta", "echo",
                                           "foxtrot", "golf",  "hotel",   "india", "juliet"};

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedDocValuesField>("nato", values[i]));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*subFsDir);
        EXPECT_EQ(reader->numDocs(), 10);

        auto leaves = reader->leaves();
        EXPECT_GE(leaves.size(), 1);

        // Verify we can read sorted DV from all segments
        int totalDocs = 0;
        for (const auto& ctx : leaves) {
            auto* leafReader = ctx.reader.get();
            ASSERT_NE(leafReader, nullptr);

            auto* dv = leafReader->getSortedDocValues("nato");
            ASSERT_NE(dv, nullptr);

            for (int i = 0; i < leafReader->maxDoc(); i++) {
                ASSERT_TRUE(dv->advanceExact(i))
                    << "doc " << i << " in segment should have sorted DV";
                int ord = dv->ordValue();
                EXPECT_GE(ord, 0);
                std::string val = bytesRefToString(dv->lookupOrd(ord));
                EXPECT_FALSE(val.empty())
                    << "doc " << i << " in segment should have non-empty value";
                totalDocs++;
            }
        }

        EXPECT_EQ(totalDocs, 10);
    }

    subFsDir->close();
}

TEST_F(SortedDocValuesTest, Iteration) {
    fs::path subDir = testDir_ / "iteration";
    fs::create_directories(subDir);
    auto subFsDir = FSDirectory::open(subDir.string());

    // Write phase - sparse: only docs 1, 3, 4 have values
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*subFsDir, config);

        for (int i = 0; i < 6; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc " + std::to_string(i), false));

            if (i == 1) {
                doc.add(std::make_unique<SortedDocValuesField>("state", "open"));
            } else if (i == 3) {
                doc.add(std::make_unique<SortedDocValuesField>("state", "closed"));
            } else if (i == 4) {
                doc.add(std::make_unique<SortedDocValuesField>("state", "open"));
            }
            // docs 0, 2, 5 have no sorted DV

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase - test nextDoc() iteration skipping docs without values
    {
        auto reader = DirectoryReader::open(*subFsDir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedDocValues("state");
        ASSERT_NE(dv, nullptr);

        // nextDoc() should skip doc 0 (no value) and land on doc 1
        int docID = dv->nextDoc();
        EXPECT_EQ(docID, 1);
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(dv->ordValue())), "open");

        // nextDoc() should skip doc 2 (no value) and land on doc 3
        docID = dv->nextDoc();
        EXPECT_EQ(docID, 3);
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(dv->ordValue())), "closed");

        // nextDoc() should land on doc 4
        docID = dv->nextDoc();
        EXPECT_EQ(docID, 4);
        EXPECT_EQ(bytesRefToString(dv->lookupOrd(dv->ordValue())), "open");

        // nextDoc() should skip doc 5 (no value) and reach NO_MORE_DOCS
        docID = dv->nextDoc();
        EXPECT_EQ(docID, search::DocIdSetIterator::NO_MORE_DOCS);
    }

    subFsDir->close();
}
