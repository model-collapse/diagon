// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

// ==================== Helpers ====================

static std::string bytesRefToString(const util::BytesRef& br) {
    return std::string(reinterpret_cast<const char*>(br.data()), br.length());
}

// ==================== Test Fixture ====================

class DocValuesMergeTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() / ("diagon_dv_merge_test_" + std::to_string(getpid()) +
                                                "_" + std::to_string(counter++));
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

// ==================== Test 1: Merge Preserves Numeric DocValues ====================

TEST_F(DocValuesMergeTest, MergePreservesNumericDocValues) {
    const int numDocs = 10;

    // Write phase: 10 docs across multiple segments (maxBufferedDocs=3)
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("price", (i + 1) * 100));
            doc.add(std::make_unique<NumericDocValuesField>("rating", i + 1));
            writer.addDocument(doc);
        }

        writer.commit();

        // Verify multiple segments exist before merge
        auto preReader = DirectoryReader::open(*dir);
        EXPECT_GE(preReader->leaves().size(), 2u) << "Should have multiple segments before merge";
        preReader->close();

        // Force merge to 1 segment
        writer.forceMerge(1);
        writer.close();
    }

    // Read phase: verify all numeric DV survive merge intact
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), numDocs);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u) << "Should have exactly 1 segment after forceMerge(1)";

        auto* leafReader = leaves[0].reader.get();
        ASSERT_NE(leafReader, nullptr);

        auto* priceDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(priceDv, nullptr);

        auto* ratingDv = leafReader->getNumericDocValues("rating");
        ASSERT_NE(ratingDv, nullptr);

        // Collect all values and verify they match expected set
        std::vector<int64_t> prices;
        std::vector<int64_t> ratings;
        for (int i = 0; i < numDocs; i++) {
            EXPECT_TRUE(priceDv->advanceExact(i)) << "Missing price at doc " << i;
            prices.push_back(priceDv->longValue());

            EXPECT_TRUE(ratingDv->advanceExact(i)) << "Missing rating at doc " << i;
            ratings.push_back(ratingDv->longValue());
        }

        // After merge, doc ordering may change. Verify all expected values are present.
        std::sort(prices.begin(), prices.end());
        std::sort(ratings.begin(), ratings.end());

        for (int i = 0; i < numDocs; i++) {
            EXPECT_EQ(prices[i], (i + 1) * 100) << "Price value missing: " << (i + 1) * 100;
            EXPECT_EQ(ratings[i], i + 1) << "Rating value missing: " << i + 1;
        }
    }
}

// ==================== Test 2: Merge Preserves Sorted DocValues ====================

TEST_F(DocValuesMergeTest, MergePreservesSortedDocValues) {
    const int numDocs = 10;

    // Write phase: 10 docs across multiple segments
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedDocValuesField>("status",
                                                           "status_" + std::to_string(i % 3)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.forceMerge(1);
        writer.close();
    }

    // Read phase: verify ordinals reassigned correctly after merge
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), numDocs);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        auto* dv = leafReader->getSortedDocValues("status");
        ASSERT_NE(dv, nullptr);

        // After merge, the global dictionary should have exactly 3 unique values
        EXPECT_EQ(dv->getValueCount(), 3);

        // Verify lookupOrd returns correct terms for all ordinals
        std::set<std::string> dictTerms;
        for (int ord = 0; ord < dv->getValueCount(); ord++) {
            dictTerms.insert(bytesRefToString(dv->lookupOrd(ord)));
        }
        EXPECT_TRUE(dictTerms.count("status_0"));
        EXPECT_TRUE(dictTerms.count("status_1"));
        EXPECT_TRUE(dictTerms.count("status_2"));

        // Verify every doc has a valid ordinal pointing to the correct term
        std::vector<std::string> docValues;
        for (int i = 0; i < numDocs; i++) {
            ASSERT_TRUE(dv->advanceExact(i)) << "Missing sorted DV at doc " << i;
            int ord = dv->ordValue();
            EXPECT_GE(ord, 0);
            EXPECT_LT(ord, dv->getValueCount());
            docValues.push_back(bytesRefToString(dv->lookupOrd(ord)));
        }

        // Count occurrences -- should match the pattern i % 3
        int count0 = 0, count1 = 0, count2 = 0;
        for (const auto& val : docValues) {
            if (val == "status_0")
                count0++;
            else if (val == "status_1")
                count1++;
            else if (val == "status_2")
                count2++;
        }
        // 10 docs with i % 3: 0,1,2,0,1,2,0,1,2,0 -> 4x status_0, 3x status_1, 3x status_2
        EXPECT_EQ(count0, 4);
        EXPECT_EQ(count1, 3);
        EXPECT_EQ(count2, 3);
    }
}

// ==================== Test 3: Merge Preserves Binary DocValues ====================

TEST_F(DocValuesMergeTest, MergePreservesBinaryDocValues) {
    const int numDocs = 10;

    // Write phase
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<BinaryDocValuesField>("data", "payload_" + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.forceMerge(1);
        writer.close();
    }

    // Read phase: verify binary data intact after merge
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), numDocs);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        auto* dv = leafReader->getBinaryDocValues("data");
        ASSERT_NE(dv, nullptr);

        // Collect all binary values
        std::set<std::string> allValues;
        for (int i = 0; i < numDocs; i++) {
            ASSERT_TRUE(dv->advanceExact(i)) << "Missing binary DV at doc " << i;
            std::string val = bytesRefToString(dv->binaryValue());
            EXPECT_TRUE(val.find("payload_") == 0)
                << "Unexpected value at doc " << i << ": " << val;
            allValues.insert(val);
        }

        // All 10 unique values should be present
        EXPECT_EQ(allValues.size(), static_cast<size_t>(numDocs));
        for (int i = 0; i < numDocs; i++) {
            EXPECT_TRUE(allValues.count("payload_" + std::to_string(i))) << "Missing payload_" << i;
        }
    }
}

// ==================== Test 4: Merge Preserves SortedNumeric DocValues ====================

TEST_F(DocValuesMergeTest, MergePreservesSortedNumericDocValues) {
    const int numDocs = 10;

    // Write phase: each doc gets 2 multi-valued numerics
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", i * 10));
            doc.add(std::make_unique<SortedNumericDocValuesField>("vals", i * 20));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.forceMerge(1);
        writer.close();
    }

    // Read phase: verify multi-valued numeric data intact
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), numDocs);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        auto* dv = leafReader->getSortedNumericDocValues("vals");
        ASSERT_NE(dv, nullptr);

        // Collect all (min, max) pairs from each doc
        std::vector<std::pair<int64_t, int64_t>> valuePairs;
        for (int i = 0; i < numDocs; i++) {
            ASSERT_TRUE(dv->advanceExact(i)) << "Missing sorted numeric DV at doc " << i;
            EXPECT_EQ(dv->docValueCount(), 2) << "Expected 2 values at doc " << i;
            int64_t v1 = dv->nextValue();
            int64_t v2 = dv->nextValue();
            // Values should be in ascending order
            EXPECT_LE(v1, v2) << "Values not sorted at doc " << i;
            valuePairs.emplace_back(std::min(v1, v2), std::max(v1, v2));
        }

        // Sort by first element and verify all expected pairs exist
        std::sort(valuePairs.begin(), valuePairs.end());
        for (int i = 0; i < numDocs; i++) {
            EXPECT_EQ(valuePairs[i].first, i * 10) << "Missing first value " << i * 10;
            EXPECT_EQ(valuePairs[i].second, i * 20) << "Missing second value " << i * 20;
        }
    }
}

// ==================== Test 5: Merge Preserves SortedSet DocValues ====================

TEST_F(DocValuesMergeTest, MergePreservesSortedSetDocValues) {
    const int numDocs = 10;

    // Write phase: each doc gets 2 sorted set values
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "tag_" + std::to_string(i)));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "common"));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.forceMerge(1);
        writer.close();
    }

    // Read phase: verify global term dict rebuilt correctly
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), numDocs);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        auto* dv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(dv, nullptr);

        // Global dictionary should have 11 unique terms: "common" + "tag_0".."tag_9"
        EXPECT_EQ(dv->getValueCount(), 11);

        // Verify the dictionary contains all expected terms
        std::set<std::string> dictTerms;
        for (int64_t ord = 0; ord < dv->getValueCount(); ord++) {
            dictTerms.insert(bytesRefToString(dv->lookupOrd(ord)));
        }
        EXPECT_TRUE(dictTerms.count("common"));
        for (int i = 0; i < numDocs; i++) {
            EXPECT_TRUE(dictTerms.count("tag_" + std::to_string(i)))
                << "Missing tag_" << i << " in global dictionary";
        }

        // Verify each doc has exactly 2 ordinals, including "common"
        for (int i = 0; i < numDocs; i++) {
            ASSERT_TRUE(dv->advanceExact(i)) << "Missing sorted set DV at doc " << i;
            std::set<std::string> docTags;
            int64_t ord;
            while ((ord = dv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                docTags.insert(bytesRefToString(dv->lookupOrd(ord)));
            }
            EXPECT_EQ(docTags.size(), 2u) << "Expected 2 tags at doc " << i;
            EXPECT_TRUE(docTags.count("common"))
                << "Doc " << i << " missing 'common' tag after merge";
        }

        // Verify all per-doc tags are present across the corpus
        std::set<std::string> allPerDocTags;
        for (int i = 0; i < numDocs; i++) {
            ASSERT_TRUE(dv->advanceExact(i));
            int64_t ord;
            while ((ord = dv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                std::string tag = bytesRefToString(dv->lookupOrd(ord));
                if (tag != "common") {
                    allPerDocTags.insert(tag);
                }
            }
        }
        EXPECT_EQ(allPerDocTags.size(), static_cast<size_t>(numDocs));
        for (int i = 0; i < numDocs; i++) {
            EXPECT_TRUE(allPerDocTags.count("tag_" + std::to_string(i)))
                << "Missing tag_" << i << " in per-doc values after merge";
        }
    }
}
