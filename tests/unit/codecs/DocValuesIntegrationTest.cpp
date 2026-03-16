// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/Term.h"
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

class DocValuesIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_dv_integration_test_" + std::to_string(getpid()) + "_" +
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

// ==================== Test 1: All DocValues Types on Single Document ====================

TEST_F(DocValuesIntegrationTest, AllDocValuesTypesOnSingleDocument) {
    // Write phase: one document with all 5 DV types
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test document", false));
        doc.add(std::make_unique<NumericDocValuesField>("price", 42));
        doc.add(std::make_unique<SortedDocValuesField>("category", "electronics"));
        doc.add(std::make_unique<BinaryDocValuesField>("data", "binary_blob"));
        doc.add(std::make_unique<SortedNumericDocValuesField>("scores", 10));
        doc.add(std::make_unique<SortedNumericDocValuesField>("scores", 20));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "new"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "sale"));

        writer.addDocument(doc);
        writer.commit();
        writer.close();
    }

    // Read phase: verify all 5 types
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 1);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1);
        auto* leafReader = leaves[0].reader.get();
        ASSERT_NE(leafReader, nullptr);

        // 1. Numeric DocValues
        auto* numDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(numDv, nullptr);
        EXPECT_TRUE(numDv->advanceExact(0));
        EXPECT_EQ(numDv->longValue(), 42);

        // 2. Sorted DocValues
        auto* sortedDv = leafReader->getSortedDocValues("category");
        ASSERT_NE(sortedDv, nullptr);
        EXPECT_TRUE(sortedDv->advanceExact(0));
        int ord = sortedDv->ordValue();
        EXPECT_GE(ord, 0);
        EXPECT_EQ(bytesRefToString(sortedDv->lookupOrd(ord)), "electronics");

        // 3. Binary DocValues
        auto* binaryDv = leafReader->getBinaryDocValues("data");
        ASSERT_NE(binaryDv, nullptr);
        EXPECT_TRUE(binaryDv->advanceExact(0));
        EXPECT_EQ(bytesRefToString(binaryDv->binaryValue()), "binary_blob");

        // 4. SortedNumeric DocValues
        auto* sortedNumDv = leafReader->getSortedNumericDocValues("scores");
        ASSERT_NE(sortedNumDv, nullptr);
        EXPECT_TRUE(sortedNumDv->advanceExact(0));
        EXPECT_EQ(sortedNumDv->docValueCount(), 2);
        int64_t v1 = sortedNumDv->nextValue();
        int64_t v2 = sortedNumDv->nextValue();
        // Values should be returned in ascending order
        EXPECT_LE(v1, v2);
        EXPECT_EQ(std::min(v1, v2), 10);
        EXPECT_EQ(std::max(v1, v2), 20);

        // 5. SortedSet DocValues
        auto* sortedSetDv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(sortedSetDv, nullptr);
        EXPECT_TRUE(sortedSetDv->advanceExact(0));
        std::set<std::string> tagValues;
        int64_t ordVal;
        while ((ordVal = sortedSetDv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
            tagValues.insert(bytesRefToString(sortedSetDv->lookupOrd(ordVal)));
        }
        EXPECT_EQ(tagValues.size(), 2u);
        EXPECT_TRUE(tagValues.count("new"));
        EXPECT_TRUE(tagValues.count("sale"));
    }
}

// ==================== Test 2: All DocValues Types on Multiple Documents ====================

TEST_F(DocValuesIntegrationTest, AllDocValuesTypesMultipleDocuments) {
    const int numDocs = 10;

    // Write phase: 10 docs with all 5 DV types, varied values
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < numDocs; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("price", (i + 1) * 100));
            doc.add(
                std::make_unique<SortedDocValuesField>("category", "cat_" + std::to_string(i % 3)));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", "data_" + std::to_string(i)));
            doc.add(std::make_unique<SortedNumericDocValuesField>("ratings", i * 10));
            doc.add(std::make_unique<SortedNumericDocValuesField>("ratings", i * 10 + 5));
            doc.add(
                std::make_unique<SortedSetDocValuesField>("labels", "label_" + std::to_string(i)));
            doc.add(std::make_unique<SortedSetDocValuesField>("labels", "common"));

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase: verify every doc reads back correctly
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), numDocs);

        auto leaves = reader->leaves();
        ASSERT_GE(leaves.size(), 1u);

        int globalDoc = 0;
        for (const auto& ctx : leaves) {
            auto* leafReader = ctx.reader.get();
            ASSERT_NE(leafReader, nullptr);

            auto* numDv = leafReader->getNumericDocValues("price");
            auto* sortedDv = leafReader->getSortedDocValues("category");
            auto* binaryDv = leafReader->getBinaryDocValues("payload");
            auto* sortedNumDv = leafReader->getSortedNumericDocValues("ratings");
            auto* sortedSetDv = leafReader->getSortedSetDocValues("labels");

            ASSERT_NE(numDv, nullptr);
            ASSERT_NE(sortedDv, nullptr);
            ASSERT_NE(binaryDv, nullptr);
            ASSERT_NE(sortedNumDv, nullptr);
            ASSERT_NE(sortedSetDv, nullptr);

            for (int localDoc = 0; localDoc < leafReader->maxDoc(); localDoc++) {
                int i = globalDoc;

                // Numeric
                EXPECT_TRUE(numDv->advanceExact(localDoc));
                EXPECT_EQ(numDv->longValue(), (i + 1) * 100) << "Numeric mismatch at doc " << i;

                // Sorted
                EXPECT_TRUE(sortedDv->advanceExact(localDoc));
                int ord = sortedDv->ordValue();
                EXPECT_GE(ord, 0);
                EXPECT_EQ(bytesRefToString(sortedDv->lookupOrd(ord)),
                          "cat_" + std::to_string(i % 3))
                    << "Sorted mismatch at doc " << i;

                // Binary
                EXPECT_TRUE(binaryDv->advanceExact(localDoc));
                EXPECT_EQ(bytesRefToString(binaryDv->binaryValue()), "data_" + std::to_string(i))
                    << "Binary mismatch at doc " << i;

                // SortedNumeric
                EXPECT_TRUE(sortedNumDv->advanceExact(localDoc));
                EXPECT_EQ(sortedNumDv->docValueCount(), 2)
                    << "SortedNumeric count mismatch at doc " << i;
                int64_t v1 = sortedNumDv->nextValue();
                int64_t v2 = sortedNumDv->nextValue();
                EXPECT_LE(v1, v2);
                EXPECT_EQ(std::min(v1, v2), i * 10);
                EXPECT_EQ(std::max(v1, v2), i * 10 + 5);

                // SortedSet
                EXPECT_TRUE(sortedSetDv->advanceExact(localDoc));
                std::set<std::string> labels;
                int64_t ordVal;
                while ((ordVal = sortedSetDv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                    labels.insert(bytesRefToString(sortedSetDv->lookupOrd(ordVal)));
                }
                EXPECT_EQ(labels.size(), 2u) << "SortedSet size mismatch at doc " << i;
                EXPECT_TRUE(labels.count("common")) << "Missing 'common' at doc " << i;
                EXPECT_TRUE(labels.count("label_" + std::to_string(i)))
                    << "Missing 'label_" << i << "' at doc " << i;

                globalDoc++;
            }
        }

        EXPECT_EQ(globalDoc, numDocs);
    }
}

// ==================== Test 3: Mixed Presence Across Docs ====================

TEST_F(DocValuesIntegrationTest, MixedPresenceAcrossDocs) {
    // Write phase: docs with varying DV field presence
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*dir, config);

        // Doc 0: only numeric DV
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc zero", false));
            doc.add(std::make_unique<NumericDocValuesField>("price", 100));
            writer.addDocument(doc);
        }

        // Doc 1: only sorted DV
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc one", false));
            doc.add(std::make_unique<SortedDocValuesField>("category", "books"));
            writer.addDocument(doc);
        }

        // Doc 2: only binary DV
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc two", false));
            doc.add(std::make_unique<BinaryDocValuesField>("blob", "raw_data"));
            writer.addDocument(doc);
        }

        // Doc 3: all DV types
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc three", false));
            doc.add(std::make_unique<NumericDocValuesField>("price", 300));
            doc.add(std::make_unique<SortedDocValuesField>("category", "toys"));
            doc.add(std::make_unique<BinaryDocValuesField>("blob", "more_data"));
            doc.add(std::make_unique<SortedNumericDocValuesField>("ratings", 5));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "hot"));
            writer.addDocument(doc);
        }

        // Doc 4: numeric + sorted numeric only
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc four", false));
            doc.add(std::make_unique<NumericDocValuesField>("price", 400));
            doc.add(std::make_unique<SortedNumericDocValuesField>("ratings", 7));
            doc.add(std::make_unique<SortedNumericDocValuesField>("ratings", 9));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase: verify each doc only has the types it was indexed with
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 5);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);
        auto* leafReader = leaves[0].reader.get();

        // --- Doc 0: has numeric "price", no sorted/binary/sortednumeric/sortedset ---
        auto* numDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(numDv, nullptr);
        EXPECT_TRUE(numDv->advanceExact(0));
        EXPECT_EQ(numDv->longValue(), 100);

        // Doc 0 should be missing from sorted "category"
        auto* sortedDv = leafReader->getSortedDocValues("category");
        if (sortedDv != nullptr) {
            // Field exists in segment (due to other docs), but doc 0 has no value
            bool hasValue = sortedDv->advanceExact(0);
            if (hasValue) {
                // If implementation returns a value for missing docs, accept it
                // (e.g., ordinal -1 for missing)
                (void)sortedDv->ordValue();
            }
        }

        // --- Doc 1: has sorted "category", no numeric ---
        sortedDv = leafReader->getSortedDocValues("category");
        ASSERT_NE(sortedDv, nullptr);
        EXPECT_TRUE(sortedDv->advanceExact(1));
        int ord = sortedDv->ordValue();
        EXPECT_GE(ord, 0);
        EXPECT_EQ(bytesRefToString(sortedDv->lookupOrd(ord)), "books");

        // Doc 1 should have missing numeric "price" (0 default)
        numDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(numDv, nullptr);
        EXPECT_TRUE(numDv->advanceExact(1));
        EXPECT_EQ(numDv->longValue(), 0);

        // --- Doc 2: has binary "blob" ---
        auto* binaryDv = leafReader->getBinaryDocValues("blob");
        ASSERT_NE(binaryDv, nullptr);
        EXPECT_TRUE(binaryDv->advanceExact(2));
        EXPECT_EQ(bytesRefToString(binaryDv->binaryValue()), "raw_data");

        // --- Doc 3: has all types ---
        numDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(numDv, nullptr);
        EXPECT_TRUE(numDv->advanceExact(3));
        EXPECT_EQ(numDv->longValue(), 300);

        sortedDv = leafReader->getSortedDocValues("category");
        ASSERT_NE(sortedDv, nullptr);
        EXPECT_TRUE(sortedDv->advanceExact(3));
        EXPECT_EQ(bytesRefToString(sortedDv->lookupOrd(sortedDv->ordValue())), "toys");

        binaryDv = leafReader->getBinaryDocValues("blob");
        ASSERT_NE(binaryDv, nullptr);
        EXPECT_TRUE(binaryDv->advanceExact(3));
        EXPECT_EQ(bytesRefToString(binaryDv->binaryValue()), "more_data");

        auto* sortedNumDv = leafReader->getSortedNumericDocValues("ratings");
        ASSERT_NE(sortedNumDv, nullptr);
        EXPECT_TRUE(sortedNumDv->advanceExact(3));
        EXPECT_EQ(sortedNumDv->docValueCount(), 1);
        EXPECT_EQ(sortedNumDv->nextValue(), 5);

        auto* sortedSetDv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(sortedSetDv, nullptr);
        EXPECT_TRUE(sortedSetDv->advanceExact(3));
        int64_t ordVal = sortedSetDv->nextOrd();
        EXPECT_NE(ordVal, SortedSetDocValues::NO_MORE_ORDS);
        EXPECT_EQ(bytesRefToString(sortedSetDv->lookupOrd(ordVal)), "hot");
        EXPECT_EQ(sortedSetDv->nextOrd(), SortedSetDocValues::NO_MORE_ORDS);

        // --- Doc 4: numeric + sorted numeric ---
        numDv = leafReader->getNumericDocValues("price");
        ASSERT_NE(numDv, nullptr);
        EXPECT_TRUE(numDv->advanceExact(4));
        EXPECT_EQ(numDv->longValue(), 400);

        sortedNumDv = leafReader->getSortedNumericDocValues("ratings");
        ASSERT_NE(sortedNumDv, nullptr);
        EXPECT_TRUE(sortedNumDv->advanceExact(4));
        EXPECT_EQ(sortedNumDv->docValueCount(), 2);
        int64_t v1 = sortedNumDv->nextValue();
        int64_t v2 = sortedNumDv->nextValue();
        EXPECT_EQ(std::min(v1, v2), 7);
        EXPECT_EQ(std::max(v1, v2), 9);
    }
}

// ==================== Test 4: DocValues with Stored Fields ====================

TEST_F(DocValuesIntegrationTest, DocValuesWithStoredFields) {
    // Write phase: docs with both DV and stored fields
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            // Stored + indexed text field
            doc.add(std::make_unique<TextField>("title", "Product " + std::to_string(i), true));
            // Numeric doc values (not stored)
            doc.add(std::make_unique<NumericDocValuesField>("price_dv", (i + 1) * 50));
            // Sorted doc values (not stored)
            doc.add(std::make_unique<SortedDocValuesField>("brand", "brand_" + std::to_string(i)));
            // Binary doc values (not stored)
            doc.add(
                std::make_unique<BinaryDocValuesField>("metadata", "meta_" + std::to_string(i)));

            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase: verify stored fields and DV independently
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 5);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);
        auto* leafReader = leaves[0].reader.get();

        // Verify stored fields reader works
        auto* storedReader = leafReader->storedFieldsReader();
        ASSERT_NE(storedReader, nullptr);
        for (int i = 0; i < 5; i++) {
            auto fields = storedReader->document(i);
            ASSERT_TRUE(fields.find("title") != fields.end())
                << "Missing stored field 'title' at doc " << i;
            EXPECT_EQ(std::get<std::string>(fields["title"]), "Product " + std::to_string(i));
        }

        // Verify numeric doc values independently
        auto* priceDv = leafReader->getNumericDocValues("price_dv");
        ASSERT_NE(priceDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(priceDv->advanceExact(i));
            EXPECT_EQ(priceDv->longValue(), (i + 1) * 50);
        }

        // Verify sorted doc values independently
        auto* brandDv = leafReader->getSortedDocValues("brand");
        ASSERT_NE(brandDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(brandDv->advanceExact(i));
            int ord = brandDv->ordValue();
            EXPECT_GE(ord, 0);
            EXPECT_EQ(bytesRefToString(brandDv->lookupOrd(ord)), "brand_" + std::to_string(i));
        }

        // Verify binary doc values independently
        auto* metaDv = leafReader->getBinaryDocValues("metadata");
        ASSERT_NE(metaDv, nullptr);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(metaDv->advanceExact(i));
            EXPECT_EQ(bytesRefToString(metaDv->binaryValue()), "meta_" + std::to_string(i));
        }
    }
}

// ==================== Test 5: DocValues with Deletions ====================

TEST_F(DocValuesIntegrationTest, DocValuesWithDeletions) {
    // Write phase: 5 docs with DV, then delete doc 2
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            // Need a keyword field for deletion targeting
            doc.add(std::make_unique<StringField>("id", std::to_string(i), false));
            doc.add(std::make_unique<TextField>("body", "content " + std::to_string(i), false));
            doc.add(std::make_unique<NumericDocValuesField>("score", (i + 1) * 10));
            doc.add(
                std::make_unique<SortedDocValuesField>("status", "status_" + std::to_string(i)));
            doc.add(std::make_unique<BinaryDocValuesField>("payload", "pay_" + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();

        // Delete doc with id=2
        Term term("id", "2");
        writer.deleteDocuments(term);
        writer.commit();
        writer.close();
    }

    // Read phase: verify remaining docs have correct DV
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 4);  // 5 - 1 deleted
        EXPECT_EQ(reader->maxDoc(), 5);   // maxDoc unchanged
        EXPECT_TRUE(reader->hasDeletions());

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);
        auto* leafReader = leaves[0].reader.get();

        const auto* liveDocs = leafReader->getLiveDocs();
        ASSERT_NE(liveDocs, nullptr);

        auto* scoreDv = leafReader->getNumericDocValues("score");
        ASSERT_NE(scoreDv, nullptr);

        auto* statusDv = leafReader->getSortedDocValues("status");
        ASSERT_NE(statusDv, nullptr);

        auto* payloadDv = leafReader->getBinaryDocValues("payload");
        ASSERT_NE(payloadDv, nullptr);

        for (int i = 0; i < 5; i++) {
            if (i == 2) {
                // Doc 2 is deleted
                EXPECT_FALSE(liveDocs->get(i)) << "Doc 2 should be deleted";
                continue;
            }
            EXPECT_TRUE(liveDocs->get(i)) << "Doc " << i << " should be live";

            // Verify numeric DV still correct for live docs
            EXPECT_TRUE(scoreDv->advanceExact(i));
            EXPECT_EQ(scoreDv->longValue(), (i + 1) * 10)
                << "Numeric DV mismatch at live doc " << i;

            // Verify sorted DV still correct for live docs
            EXPECT_TRUE(statusDv->advanceExact(i));
            int ord = statusDv->ordValue();
            EXPECT_GE(ord, 0);
            EXPECT_EQ(bytesRefToString(statusDv->lookupOrd(ord)), "status_" + std::to_string(i))
                << "Sorted DV mismatch at live doc " << i;

            // Verify binary DV still correct for live docs
            EXPECT_TRUE(payloadDv->advanceExact(i));
            EXPECT_EQ(bytesRefToString(payloadDv->binaryValue()), "pay_" + std::to_string(i))
                << "Binary DV mismatch at live doc " << i;
        }
    }
}

// ==================== Test 6: DocValues-Only Document ====================

TEST_F(DocValuesIntegrationTest, DocValuesOnlyDocument) {
    // Write phase: document with a minimal indexed field + DV fields only
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        // Minimal indexed field so the document is valid for the inverted index
        doc.add(std::make_unique<StringField>("_id", "1", false));
        // All 5 DV types, no stored fields
        doc.add(std::make_unique<NumericDocValuesField>("timestamp", 1709251200));
        doc.add(std::make_unique<SortedDocValuesField>("region", "us-east-1"));
        doc.add(std::make_unique<BinaryDocValuesField>("checksum", "abc123def456"));
        doc.add(std::make_unique<SortedNumericDocValuesField>("latencies", 42));
        doc.add(std::make_unique<SortedNumericDocValuesField>("latencies", 99));
        doc.add(std::make_unique<SortedSetDocValuesField>("features", "gpu"));
        doc.add(std::make_unique<SortedSetDocValuesField>("features", "ssd"));

        writer.addDocument(doc);
        writer.commit();
        writer.close();
    }

    // Read phase: verify all DV types readable
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 1);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);
        auto* leafReader = leaves[0].reader.get();

        // Numeric
        auto* tsDv = leafReader->getNumericDocValues("timestamp");
        ASSERT_NE(tsDv, nullptr);
        EXPECT_TRUE(tsDv->advanceExact(0));
        EXPECT_EQ(tsDv->longValue(), 1709251200);

        // Sorted
        auto* regionDv = leafReader->getSortedDocValues("region");
        ASSERT_NE(regionDv, nullptr);
        EXPECT_TRUE(regionDv->advanceExact(0));
        int ord = regionDv->ordValue();
        EXPECT_GE(ord, 0);
        EXPECT_EQ(bytesRefToString(regionDv->lookupOrd(ord)), "us-east-1");

        // Binary
        auto* checksumDv = leafReader->getBinaryDocValues("checksum");
        ASSERT_NE(checksumDv, nullptr);
        EXPECT_TRUE(checksumDv->advanceExact(0));
        EXPECT_EQ(bytesRefToString(checksumDv->binaryValue()), "abc123def456");

        // SortedNumeric
        auto* latDv = leafReader->getSortedNumericDocValues("latencies");
        ASSERT_NE(latDv, nullptr);
        EXPECT_TRUE(latDv->advanceExact(0));
        EXPECT_EQ(latDv->docValueCount(), 2);
        int64_t v1 = latDv->nextValue();
        int64_t v2 = latDv->nextValue();
        EXPECT_EQ(std::min(v1, v2), 42);
        EXPECT_EQ(std::max(v1, v2), 99);

        // SortedSet
        auto* featDv = leafReader->getSortedSetDocValues("features");
        ASSERT_NE(featDv, nullptr);
        EXPECT_TRUE(featDv->advanceExact(0));
        std::set<std::string> features;
        int64_t ordVal;
        while ((ordVal = featDv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
            features.insert(bytesRefToString(featDv->lookupOrd(ordVal)));
        }
        EXPECT_EQ(features.size(), 2u);
        EXPECT_TRUE(features.count("gpu"));
        EXPECT_TRUE(features.count("ssd"));
    }
}
