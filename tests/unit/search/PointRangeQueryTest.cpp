// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include <gtest/gtest.h>

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/PointRangeQuery.h"
#include "diagon/search/TopDocs.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/util/NumericUtils.h"

#include <filesystem>

namespace fs = std::filesystem;
using namespace diagon;

class PointRangeQueryTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / ("prq_test_" + std::to_string(getpid()));
        fs::create_directories(testDir_);
        directory_ = store::FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        directory_.reset();
        fs::remove_all(testDir_);
    }

    fs::path testDir_;
    std::unique_ptr<store::Directory> directory_;
};

TEST_F(PointRangeQueryTest, EndToEndLongRange) {
    // Index documents with LongPointField
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 100; i++) {
            document::Document doc;
            doc.add(std::make_unique<document::LongPointField>("price",
                                                                static_cast<int64_t>(i * 10)));
            doc.add(std::make_unique<document::TextField>("body", "test document " +
                                                                       std::to_string(i),
                                                           document::TextField::TYPE_STORED));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    // Search with PointRangeQuery
    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Range [200, 500] should match docs with price 200, 210, ..., 500 = 31 docs
    auto query = search::PointRangeQuery::newLongRange("price", 200, 500);

    auto topDocs = searcher.search(*query, 100);

    EXPECT_EQ(topDocs.totalHits.value, 31) << "Expected 31 hits for price in [200, 500]";
}

TEST_F(PointRangeQueryTest, EndToEndDoubleRange) {
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 50; i++) {
            document::Document doc;
            doc.add(std::make_unique<document::DoublePointField>("score",
                                                                  static_cast<double>(i) * 0.5));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Range [5.0, 10.0] => values 5.0, 5.5, 6.0, ..., 10.0 => 11 docs
    auto query = search::PointRangeQuery::newDoubleRange("score", 5.0, 10.0);

    auto topDocs = searcher.search(*query, 100);

    EXPECT_EQ(topDocs.totalHits.value, 11) << "Expected 11 hits for score in [5.0, 10.0]";
}

TEST_F(PointRangeQueryTest, ExactMatch) {
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 10; i++) {
            document::Document doc;
            doc.add(std::make_unique<document::LongPointField>("id", static_cast<int64_t>(i)));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Exact match: [5, 5]
    auto query = search::PointRangeQuery::newLongRange("id", 5, 5);

    auto topDocs = searcher.search(*query, 10);

    EXPECT_EQ(topDocs.totalHits.value, 1) << "Expected exactly 1 hit for id=5";
}

TEST_F(PointRangeQueryTest, EmptyRange) {
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 10; i++) {
            document::Document doc;
            doc.add(std::make_unique<document::LongPointField>("val", static_cast<int64_t>(i)));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Range [100, 200] — no values in this range
    auto query = search::PointRangeQuery::newLongRange("val", 100, 200);

    auto topDocs = searcher.search(*query, 10);

    EXPECT_EQ(topDocs.totalHits.value, 0) << "Expected 0 hits for out-of-range query";
}

TEST_F(PointRangeQueryTest, NonExistentField) {
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        document::Document doc;
        doc.add(std::make_unique<document::LongPointField>("price", 42));
        writer.addDocument(doc);
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Query a field that doesn't exist
    auto query = search::PointRangeQuery::newLongRange("missing_field", 0, 100);

    auto topDocs = searcher.search(*query, 10);

    EXPECT_EQ(topDocs.totalHits.value, 0);
}

TEST_F(PointRangeQueryTest, QueryToString) {
    auto query = search::PointRangeQuery::newLongRange("price", 100, 500);
    std::string str = query->toString("price");
    EXPECT_EQ(str, "[100 TO 500]");

    std::string str2 = query->toString("");
    EXPECT_EQ(str2, "price:[100 TO 500]");
}

TEST_F(PointRangeQueryTest, QueryEquality) {
    auto q1 = search::PointRangeQuery::newLongRange("price", 100, 500);
    auto q2 = search::PointRangeQuery::newLongRange("price", 100, 500);
    auto q3 = search::PointRangeQuery::newLongRange("price", 100, 600);

    EXPECT_TRUE(q1->equals(*q2));
    EXPECT_FALSE(q1->equals(*q3));
    EXPECT_EQ(q1->hashCode(), q2->hashCode());
}

TEST_F(PointRangeQueryTest, NumericRangeQueryFallbackToBKD) {
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 50; i++) {
            document::Document doc;
            doc.add(std::make_unique<document::LongPointField>("val", static_cast<int64_t>(i)));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Use PointRangeQuery directly
    auto prq = search::PointRangeQuery::newLongRange("val", 10, 30);
    auto topDocs = searcher.search(*prq, 100);

    EXPECT_EQ(topDocs.totalHits.value, 21) << "Expected 21 hits for val in [10, 30]";
}

TEST_F(PointRangeQueryTest, LongPointFieldEncoding) {
    // Verify LongPointField correctly encodes values
    document::LongPointField field("test", 12345);

    EXPECT_EQ(field.name(), "test");
    EXPECT_TRUE(field.fieldType().hasPointValues());
    EXPECT_EQ(field.fieldType().pointDimensionCount, 1);
    EXPECT_EQ(field.fieldType().pointNumBytes, 8);

    auto bv = field.binaryValue();
    ASSERT_TRUE(bv.has_value());
    EXPECT_EQ(bv->length(), 8u);

    int64_t decoded = util::NumericUtils::bytesToLongBE(bv->data());
    EXPECT_EQ(decoded, 12345);
}

TEST_F(PointRangeQueryTest, DoublePointFieldEncoding) {
    document::DoublePointField field("score", 3.14);

    auto bv = field.binaryValue();
    ASSERT_TRUE(bv.has_value());
    EXPECT_EQ(bv->length(), 8u);

    int64_t encoded = util::NumericUtils::bytesToLongBE(bv->data());
    double decoded = util::NumericUtils::sortableLongToDouble(encoded);
    EXPECT_DOUBLE_EQ(decoded, 3.14);
}
