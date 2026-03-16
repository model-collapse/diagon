// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#include <unistd.h>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

class SortedNumericDocValuesTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_sorted_numeric_dv_test_" + std::to_string(getpid()) + "_" +
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

TEST_F(SortedNumericDocValuesTest, SingleValuePerDoc) {
    // Write phase: 5 docs, each with one SortedNumericDocValuesField
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedNumericDocValuesField>("score", (i + 1) * 100));
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
        ASSERT_EQ(leaves.size(), 1);

        auto* leafReader = leaves[0].reader.get();
        ASSERT_NE(leafReader, nullptr);

        auto* dv = leafReader->getSortedNumericDocValues("score");
        ASSERT_NE(dv, nullptr);

        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(dv->advanceExact(i));
            EXPECT_EQ(dv->docValueCount(), 1);
            EXPECT_EQ(dv->nextValue(), (i + 1) * 100);
        }
    }
}

TEST_F(SortedNumericDocValuesTest, MultipleValuesPerDoc) {
    // Write phase: one doc with 3 values added in unsorted order
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "multi-valued doc", false));
        doc.add(std::make_unique<SortedNumericDocValuesField>("tags", 30));
        doc.add(std::make_unique<SortedNumericDocValuesField>("tags", 10));
        doc.add(std::make_unique<SortedNumericDocValuesField>("tags", 20));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase: values must be returned in ascending sorted order
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1);

        auto* leafReader = leaves[0].reader.get();
        auto* dv = leafReader->getSortedNumericDocValues("tags");
        ASSERT_NE(dv, nullptr);

        EXPECT_TRUE(dv->advanceExact(0));
        EXPECT_EQ(dv->docValueCount(), 3);
        EXPECT_EQ(dv->nextValue(), 10);
        EXPECT_EQ(dv->nextValue(), 20);
        EXPECT_EQ(dv->nextValue(), 30);
    }
}

TEST_F(SortedNumericDocValuesTest, SparseValues) {
    // Write phase: 10 docs, only even docs have sorted numeric DV
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));

            if (i % 2 == 0) {
                doc.add(std::make_unique<SortedNumericDocValuesField>("rating", i * 10));
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

        auto* dv = leafReader->getSortedNumericDocValues("rating");
        ASSERT_NE(dv, nullptr);

        for (int i = 0; i < 10; i++) {
            bool hasValue = dv->advanceExact(i);
            if (i % 2 == 0) {
                // Even docs have values
                EXPECT_TRUE(hasValue) << "Doc " << i << " should have a value";
                EXPECT_EQ(dv->docValueCount(), 1) << "Doc " << i;
                EXPECT_EQ(dv->nextValue(), i * 10) << "Doc " << i;
            } else {
                // Odd docs have no values — advanceExact returns false
                EXPECT_FALSE(hasValue) << "Doc " << i << " should NOT have a value";
                EXPECT_EQ(dv->docValueCount(), 0) << "Doc " << i;
            }
        }
    }
}

TEST_F(SortedNumericDocValuesTest, WriteAndReadMultipleFields) {
    // Write phase: two sorted numeric fields across 5 docs
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedNumericDocValuesField>("price", (i + 1) * 100));
            doc.add(std::make_unique<SortedNumericDocValuesField>("quantity", (i + 1) * 10));
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

        auto* leafReader = leaves[0].reader.get();

        // Verify price field
        auto* priceDv = leafReader->getSortedNumericDocValues("price");
        ASSERT_NE(priceDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(priceDv->advanceExact(i));
            EXPECT_EQ(priceDv->docValueCount(), 1);
            EXPECT_EQ(priceDv->nextValue(), (i + 1) * 100);
        }

        // Verify quantity field
        auto* quantityDv = leafReader->getSortedNumericDocValues("quantity");
        ASSERT_NE(quantityDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(quantityDv->advanceExact(i));
            EXPECT_EQ(quantityDv->docValueCount(), 1);
            EXPECT_EQ(quantityDv->nextValue(), (i + 1) * 10);
        }
    }
}

TEST_F(SortedNumericDocValuesTest, VariableValueCounts) {
    // Write phase:
    //   doc 0: 1 value  [42]
    //   doc 1: 3 values [5, 10, 15] (added unsorted)
    //   doc 2: 0 values (no sorted numeric field)
    //   doc 3: 5 values [1, 2, 3, 4, 5] (added in reverse order)
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Doc 0: 1 value
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc zero", false));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 42));
            writer.addDocument(doc);
        }

        // Doc 1: 3 values (add in unsorted order)
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc one", false));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 15));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 5));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 10));
            writer.addDocument(doc);
        }

        // Doc 2: 0 values (no sorted numeric field at all)
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc two", false));
            writer.addDocument(doc);
        }

        // Doc 3: 5 values (add in reverse order)
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc three", false));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 5));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 3));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 1));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 4));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", 2));
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

        auto* dv = leafReader->getSortedNumericDocValues("vals");
        ASSERT_NE(dv, nullptr);

        // Doc 0: 1 value [42]
        EXPECT_TRUE(dv->advanceExact(0));
        EXPECT_EQ(dv->docValueCount(), 1);
        EXPECT_EQ(dv->nextValue(), 42);

        // Doc 1: 3 values [5, 10, 15] sorted ascending
        EXPECT_TRUE(dv->advanceExact(1));
        EXPECT_EQ(dv->docValueCount(), 3);
        EXPECT_EQ(dv->nextValue(), 5);
        EXPECT_EQ(dv->nextValue(), 10);
        EXPECT_EQ(dv->nextValue(), 15);

        // Doc 2: 0 values — advanceExact returns false
        EXPECT_FALSE(dv->advanceExact(2));
        EXPECT_EQ(dv->docValueCount(), 0);

        // Doc 3: 5 values [1, 2, 3, 4, 5] sorted ascending
        EXPECT_TRUE(dv->advanceExact(3));
        EXPECT_EQ(dv->docValueCount(), 5);
        EXPECT_EQ(dv->nextValue(), 1);
        EXPECT_EQ(dv->nextValue(), 2);
        EXPECT_EQ(dv->nextValue(), 3);
        EXPECT_EQ(dv->nextValue(), 4);
        EXPECT_EQ(dv->nextValue(), 5);
    }
}

TEST_F(SortedNumericDocValuesTest, LargeValues) {
    // Write phase: INT64_MAX, INT64_MIN, and boundary values
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        // Doc 0: INT64_MAX
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "max", false));
            doc.add(std::make_unique<SortedNumericDocValuesField>(
                "extreme", std::numeric_limits<int64_t>::max()));
            writer.addDocument(doc);
        }

        // Doc 1: INT64_MIN
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "min", false));
            doc.add(std::make_unique<SortedNumericDocValuesField>(
                "extreme", std::numeric_limits<int64_t>::min()));
            writer.addDocument(doc);
        }

        // Doc 2: both extremes plus zero in one doc (should sort MIN, 0, MAX)
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "both", false));
            doc.add(std::make_unique<SortedNumericDocValuesField>(
                "extreme", std::numeric_limits<int64_t>::max()));
            doc.add(std::make_unique<SortedNumericDocValuesField>(
                "extreme", std::numeric_limits<int64_t>::min()));
            doc.add(std::make_unique<SortedNumericDocValuesField>("extreme", 0));
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

        auto* dv = leafReader->getSortedNumericDocValues("extreme");
        ASSERT_NE(dv, nullptr);

        // Doc 0: INT64_MAX
        EXPECT_TRUE(dv->advanceExact(0));
        EXPECT_EQ(dv->docValueCount(), 1);
        EXPECT_EQ(dv->nextValue(), std::numeric_limits<int64_t>::max());

        // Doc 1: INT64_MIN
        EXPECT_TRUE(dv->advanceExact(1));
        EXPECT_EQ(dv->docValueCount(), 1);
        EXPECT_EQ(dv->nextValue(), std::numeric_limits<int64_t>::min());

        // Doc 2: sorted ascending [INT64_MIN, 0, INT64_MAX]
        EXPECT_TRUE(dv->advanceExact(2));
        EXPECT_EQ(dv->docValueCount(), 3);
        EXPECT_EQ(dv->nextValue(), std::numeric_limits<int64_t>::min());
        EXPECT_EQ(dv->nextValue(), 0);
        EXPECT_EQ(dv->nextValue(), std::numeric_limits<int64_t>::max());
    }
}

TEST_F(SortedNumericDocValuesTest, DuplicateValuesInDoc) {
    // Write phase: same value added twice to same doc
    // SortedNumeric keeps duplicates (unlike SortedSet which deduplicates)
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "duplicates", false));
        doc.add(std::make_unique<SortedNumericDocValuesField>("scores", 42));
        doc.add(std::make_unique<SortedNumericDocValuesField>("scores", 42));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase: both 42s should be present
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedNumericDocValues("scores");
        ASSERT_NE(dv, nullptr);

        EXPECT_TRUE(dv->advanceExact(0));
        EXPECT_EQ(dv->docValueCount(), 2);
        EXPECT_EQ(dv->nextValue(), 42);
        EXPECT_EQ(dv->nextValue(), 42);
    }
}

TEST_F(SortedNumericDocValuesTest, NonExistentField) {
    // Write phase
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test", false));
        doc.add(std::make_unique<SortedNumericDocValuesField>("existing", 100));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        // Request non-existent field — should return nullptr
        auto* dv = leafReader->getSortedNumericDocValues("nonexistent");
        EXPECT_EQ(dv, nullptr);

        // Request field that exists — should return non-null
        auto* existingDv = leafReader->getSortedNumericDocValues("existing");
        EXPECT_NE(existingDv, nullptr);
    }
}

TEST_F(SortedNumericDocValuesTest, MultipleSegments) {
    // Write phase: maxBufferedDocs=3, 10 docs -> multiple segments
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            // Add 1-3 values per doc depending on index
            int numValues = (i % 3) + 1;
            for (int v = 0; v < numValues; v++) {
                doc.add(std::make_unique<SortedNumericDocValuesField>(
                    "multi", static_cast<int64_t>(i * 100 + v)));
            }
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase: verify across all segments
    {
        auto reader = DirectoryReader::open(*dir);
        EXPECT_EQ(reader->numDocs(), 10);

        auto leaves = reader->leaves();
        EXPECT_GE(leaves.size(), 1);

        int totalDocs = 0;
        for (const auto& ctx : leaves) {
            auto* leafReader = ctx.reader.get();
            ASSERT_NE(leafReader, nullptr);

            auto* dv = leafReader->getSortedNumericDocValues("multi");
            ASSERT_NE(dv, nullptr);

            for (int i = 0; i < leafReader->maxDoc(); i++) {
                bool hasValue = dv->advanceExact(i);
                EXPECT_TRUE(hasValue) << "All docs should have values";
                int count = dv->docValueCount();
                EXPECT_GE(count, 1);
                EXPECT_LE(count, 3);

                // Read all values and verify ascending order
                int64_t prev = std::numeric_limits<int64_t>::min();
                for (int v = 0; v < count; v++) {
                    int64_t val = dv->nextValue();
                    EXPECT_GE(val, prev) << "Values must be in ascending order";
                    prev = val;
                }

                totalDocs++;
            }
        }

        EXPECT_EQ(totalDocs, 10);
    }
}

TEST_F(SortedNumericDocValuesTest, ManyValuesPerDoc) {
    // Write phase: one doc with 100 values (0 through 99) added in random order
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        // Generate shuffled values 0..99
        std::vector<int64_t> values(100);
        std::iota(values.begin(), values.end(), 0);

        std::mt19937 rng(42);  // Fixed seed for reproducibility
        std::shuffle(values.begin(), values.end(), rng);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "many values", false));
        for (int64_t v : values) {
            doc.add(std::make_unique<SortedNumericDocValuesField>("data", v));
        }
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase: all 100 values must be returned in sorted ascending order
    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto* leafReader = leaves[0].reader.get();

        auto* dv = leafReader->getSortedNumericDocValues("data");
        ASSERT_NE(dv, nullptr);

        EXPECT_TRUE(dv->advanceExact(0));
        EXPECT_EQ(dv->docValueCount(), 100);

        for (int64_t expected = 0; expected < 100; expected++) {
            EXPECT_EQ(dv->nextValue(), expected)
                << "Value at position " << expected << " should be " << expected;
        }
    }
}
