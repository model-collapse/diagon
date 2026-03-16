// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

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

static std::string bytesRefToString(const util::BytesRef& br) {
    return std::string(reinterpret_cast<const char*>(br.data()), br.length());
}

class SortedSetDocValuesTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        testDir_ = fs::temp_directory_path() /
                   ("diagon_sorted_set_dv_test_" + std::to_string(getpid()) + "_" +
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

// ==================== Test 1: SingleValuePerDoc ====================

TEST_F(SortedSetDocValuesTest, SingleValuePerDoc) {
    // Write phase: 5 docs, each with one SortedSetDocValuesField
    std::vector<std::string> values = {"apple", "banana", "cherry", "date", "elderberry"};
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", values[i]));
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

        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        // Each doc should have exactly one ordinal, then NO_MORE_ORDS
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(ssdv->advanceExact(i));

            int64_t ord = ssdv->nextOrd();
            EXPECT_NE(ord, SortedSetDocValues::NO_MORE_ORDS)
                << "Doc " << i << " should have one ordinal";

            // Verify the term for this ordinal matches what we wrote
            std::string term = bytesRefToString(ssdv->lookupOrd(ord));
            EXPECT_EQ(term, values[i]) << "Doc " << i << " term mismatch";

            // Should be NO_MORE_ORDS after the single value
            int64_t nextOrd = ssdv->nextOrd();
            EXPECT_EQ(nextOrd, SortedSetDocValues::NO_MORE_ORDS)
                << "Doc " << i << " should have only one ordinal";
        }
    }
}

// ==================== Test 2: MultipleValuesPerDoc ====================

TEST_F(SortedSetDocValuesTest, MultipleValuesPerDoc) {
    // Write phase: one doc with ("tags","red"), ("tags","green"), ("tags","blue")
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "colorful document", false));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "red"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "green"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "blue"));
        writer.addDocument(doc);

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 1);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        EXPECT_TRUE(ssdv->advanceExact(0));

        // Collect all ordinals and their terms
        std::vector<std::string> terms;
        int64_t ord;
        while ((ord = ssdv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
            terms.push_back(bytesRefToString(ssdv->lookupOrd(ord)));
        }

        // Should have exactly 3 values in lexicographic order: blue, green, red
        ASSERT_EQ(terms.size(), 3u);
        EXPECT_EQ(terms[0], "blue");
        EXPECT_EQ(terms[1], "green");
        EXPECT_EQ(terms[2], "red");
    }
}

// ==================== Test 3: DeduplicationWithinDoc ====================

TEST_F(SortedSetDocValuesTest, DeduplicationWithinDoc) {
    // Write phase: add ("tags","alpha"), ("tags","alpha"), ("tags","beta")
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "dedup test", false));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "alpha"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "alpha"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "beta"));
        writer.addDocument(doc);

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
        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        EXPECT_TRUE(ssdv->advanceExact(0));

        // Collect all ordinals
        std::vector<std::string> terms;
        int64_t ord;
        while ((ord = ssdv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
            terms.push_back(bytesRefToString(ssdv->lookupOrd(ord)));
        }

        // Only 2 ordinals (alpha deduplicated), not 3
        ASSERT_EQ(terms.size(), 2u) << "Duplicate 'alpha' should be deduplicated";
        EXPECT_EQ(terms[0], "alpha");
        EXPECT_EQ(terms[1], "beta");
    }
}

// ==================== Test 4: SparseValues ====================

TEST_F(SortedSetDocValuesTest, SparseValues) {
    // Write phase: 10 docs, only even docs have sorted set values
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(20);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));

            // Only even documents get sorted set values
            if (i % 2 == 0) {
                doc.add(std::make_unique<SortedSetDocValuesField>("tags",
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
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 10);

        auto leaves = reader->leaves();
        ASSERT_EQ(leaves.size(), 1u);

        auto* leafReader = leaves[0].reader.get();
        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        for (int i = 0; i < 10; i++) {
            bool hasValue = ssdv->advanceExact(i);

            if (i % 2 == 0) {
                // Even docs should have a value
                EXPECT_TRUE(hasValue) << "Even doc " << i << " should have a value";
                if (hasValue) {
                    int64_t ord = ssdv->nextOrd();
                    EXPECT_NE(ord, SortedSetDocValues::NO_MORE_ORDS)
                        << "Even doc " << i << " should have an ordinal";
                    if (ord != SortedSetDocValues::NO_MORE_ORDS) {
                        std::string term = bytesRefToString(ssdv->lookupOrd(ord));
                        EXPECT_EQ(term, "value_" + std::to_string(i))
                            << "Doc " << i << " term mismatch";
                    }
                    // Should be NO_MORE_ORDS after the single value
                    EXPECT_EQ(ssdv->nextOrd(), SortedSetDocValues::NO_MORE_ORDS);
                }
            } else {
                // Odd docs should have NO values — either advanceExact returns false,
                // or nextOrd() returns NO_MORE_ORDS immediately
                if (hasValue) {
                    EXPECT_EQ(ssdv->nextOrd(), SortedSetDocValues::NO_MORE_ORDS)
                        << "Odd doc " << i << " should have NO_MORE_ORDS immediately";
                }
            }
        }
    }
}

// ==================== Test 5: WriteAndReadMultipleFields ====================

TEST_F(SortedSetDocValuesTest, WriteAndReadMultipleFields) {
    // Write phase: two sorted set fields across documents
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 5; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));

            // Field "colors"
            doc.add(
                std::make_unique<SortedSetDocValuesField>("colors", (i % 2 == 0) ? "red" : "blue"));
            doc.add(std::make_unique<SortedSetDocValuesField>("colors", "green"));

            // Field "sizes"
            doc.add(
                std::make_unique<SortedSetDocValuesField>("sizes", (i < 3) ? "small" : "large"));
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

        // Verify "colors" field
        auto* colorsDv = leafReader->getSortedSetDocValues("colors");
        ASSERT_NE(colorsDv, nullptr);

        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(colorsDv->advanceExact(i));

            std::vector<std::string> terms;
            int64_t ord;
            while ((ord = colorsDv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                terms.push_back(bytesRefToString(colorsDv->lookupOrd(ord)));
            }

            ASSERT_EQ(terms.size(), 2u) << "Doc " << i << " should have 2 colors";
            // Terms should be in sorted order
            EXPECT_TRUE(terms[0] < terms[1])
                << "Doc " << i << " colors not sorted: " << terms[0] << " >= " << terms[1];

            // Verify expected terms
            std::set<std::string> termSet(terms.begin(), terms.end());
            EXPECT_EQ(termSet.count("green"), 1u) << "Doc " << i << " should contain 'green'";
            if (i % 2 == 0) {
                EXPECT_EQ(termSet.count("red"), 1u) << "Doc " << i << " should contain 'red'";
            } else {
                EXPECT_EQ(termSet.count("blue"), 1u) << "Doc " << i << " should contain 'blue'";
            }
        }

        // Verify "sizes" field
        auto* sizesDv = leafReader->getSortedSetDocValues("sizes");
        ASSERT_NE(sizesDv, nullptr);

        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(sizesDv->advanceExact(i));

            int64_t ord = sizesDv->nextOrd();
            EXPECT_NE(ord, SortedSetDocValues::NO_MORE_ORDS);
            std::string term = bytesRefToString(sizesDv->lookupOrd(ord));

            if (i < 3) {
                EXPECT_EQ(term, "small") << "Doc " << i << " should be 'small'";
            } else {
                EXPECT_EQ(term, "large") << "Doc " << i << " should be 'large'";
            }

            // Single value per doc for sizes
            EXPECT_EQ(sizesDv->nextOrd(), SortedSetDocValues::NO_MORE_ORDS);
        }
    }
}

// ==================== Test 6: GlobalTermDictionary ====================

TEST_F(SortedSetDocValuesTest, GlobalTermDictionary) {
    // Write phase: doc0 has {"a","b"}, doc1 has {"b","c"}
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        // Doc 0: {"a", "b"}
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc zero", false));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "a"));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "b"));
            writer.addDocument(doc);
        }

        // Doc 1: {"b", "c"}
        {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "doc one", false));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "b"));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "c"));
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
        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        // Global dictionary should have 3 unique terms: "a", "b", "c"
        EXPECT_EQ(ssdv->getValueCount(), 3)
            << "Global dictionary should contain exactly 3 unique terms";

        // Verify doc 0 has {"a", "b"}
        EXPECT_TRUE(ssdv->advanceExact(0));
        {
            std::vector<std::string> terms;
            int64_t ord;
            while ((ord = ssdv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                terms.push_back(bytesRefToString(ssdv->lookupOrd(ord)));
            }
            ASSERT_EQ(terms.size(), 2u);
            EXPECT_EQ(terms[0], "a");
            EXPECT_EQ(terms[1], "b");
        }

        // Verify doc 1 has {"b", "c"}
        EXPECT_TRUE(ssdv->advanceExact(1));
        {
            std::vector<std::string> terms;
            int64_t ord;
            while ((ord = ssdv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                terms.push_back(bytesRefToString(ssdv->lookupOrd(ord)));
            }
            ASSERT_EQ(terms.size(), 2u);
            EXPECT_EQ(terms[0], "b");
            EXPECT_EQ(terms[1], "c");
        }
    }
}

// ==================== Test 7: LookupOrd ====================

TEST_F(SortedSetDocValuesTest, LookupOrd) {
    // Write phase: multiple terms to build a rich dictionary
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test doc", false));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "cherry"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "apple"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "banana"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "elderberry"));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "date"));
        writer.addDocument(doc);

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
        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        int64_t valueCount = ssdv->getValueCount();
        EXPECT_EQ(valueCount, 5);

        // lookupOrd should return terms in lexicographic order:
        // ord 0 = "apple", ord 1 = "banana", ord 2 = "cherry",
        // ord 3 = "date", ord 4 = "elderberry"
        std::vector<std::string> expected = {"apple", "banana", "cherry", "date", "elderberry"};

        for (int64_t i = 0; i < valueCount; i++) {
            std::string term = bytesRefToString(ssdv->lookupOrd(i));
            EXPECT_EQ(term, expected[static_cast<size_t>(i)])
                << "lookupOrd(" << i << ") should return '" << expected[static_cast<size_t>(i)]
                << "' but got '" << term << "'";
        }

        // Additionally verify that ordinals from nextOrd() produce sorted terms
        EXPECT_TRUE(ssdv->advanceExact(0));
        std::string prev;
        int64_t ord;
        while ((ord = ssdv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
            std::string term = bytesRefToString(ssdv->lookupOrd(ord));
            if (!prev.empty()) {
                EXPECT_LT(prev, term) << "Ordinals should produce terms in ascending lex order: '"
                                      << prev << "' should be < '" << term << "'";
            }
            prev = term;
        }
    }
}

// ==================== Test 8: EmptyStringValue ====================

TEST_F(SortedSetDocValuesTest, EmptyStringValue) {
    // Empty strings are treated as missing values by Field::stringValue() (returns nullopt).
    // This test verifies that only non-empty values are stored.
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(10);

        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "empty value test", false));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", ""));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "nonempty"));
        writer.addDocument(doc);

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
        auto* ssdv = leafReader->getSortedSetDocValues("tags");
        ASSERT_NE(ssdv, nullptr);

        // Empty string is treated as missing, so only "nonempty" is stored
        EXPECT_EQ(ssdv->getValueCount(), 1)
            << "Dictionary should contain 1 term (empty string is treated as missing)";

        EXPECT_TRUE(ssdv->advanceExact(0));

        int64_t ord0 = ssdv->nextOrd();
        EXPECT_NE(ord0, SortedSetDocValues::NO_MORE_ORDS);
        std::string term0 = bytesRefToString(ssdv->lookupOrd(ord0));
        EXPECT_EQ(term0, "nonempty");

        // No more ordinals
        EXPECT_EQ(ssdv->nextOrd(), SortedSetDocValues::NO_MORE_ORDS);
    }
}

// ==================== Test 9: NonExistentField ====================

TEST_F(SortedSetDocValuesTest, NonExistentField) {
    // Write phase
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        Document doc;
        doc.add(std::make_unique<TextField>("body", "test", false));
        doc.add(std::make_unique<SortedSetDocValuesField>("tags", "value"));
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
        auto* dv = leafReader->getSortedSetDocValues("nonexistent");
        EXPECT_EQ(dv, nullptr);

        // Request field that exists - should return non-null
        auto* tagsDv = leafReader->getSortedSetDocValues("tags");
        EXPECT_NE(tagsDv, nullptr);
    }
}

// ==================== Test 10: MultipleSegments ====================

TEST_F(SortedSetDocValuesTest, MultipleSegments) {
    // Write phase: maxBufferedDocs=3 with 10 docs to force multiple segments
    {
        IndexWriterConfig config;
        config.setMaxBufferedDocs(3);

        IndexWriter writer(*dir, config);

        for (int i = 0; i < 10; i++) {
            Document doc;
            doc.add(std::make_unique<TextField>("body", "document " + std::to_string(i), false));
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "term_" + std::to_string(i)));
            // Add a shared term to test cross-segment dictionary
            doc.add(std::make_unique<SortedSetDocValuesField>("tags", "common"));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Read phase
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_NE(reader, nullptr);
        EXPECT_EQ(reader->numDocs(), 10);

        auto leaves = reader->leaves();
        EXPECT_GE(leaves.size(), 1u) << "Should have at least one segment";

        // Verify we can read from all segments and every doc has values
        int totalDocs = 0;
        for (const auto& ctx : leaves) {
            auto* leafReader = ctx.reader.get();
            ASSERT_NE(leafReader, nullptr);

            auto* ssdv = leafReader->getSortedSetDocValues("tags");
            ASSERT_NE(ssdv, nullptr);

            for (int i = 0; i < leafReader->maxDoc(); i++) {
                EXPECT_TRUE(ssdv->advanceExact(i));

                std::vector<std::string> terms;
                int64_t ord;
                while ((ord = ssdv->nextOrd()) != SortedSetDocValues::NO_MORE_ORDS) {
                    terms.push_back(bytesRefToString(ssdv->lookupOrd(ord)));
                }

                // Each doc should have exactly 2 values: "common" and "term_N"
                ASSERT_EQ(terms.size(), 2u)
                    << "Segment doc " << i << " should have 2 sorted set values";

                // "common" sorts before "term_*" lexicographically
                EXPECT_EQ(terms[0], "common")
                    << "First term should be 'common' for segment doc " << i;
                EXPECT_TRUE(terms[1].find("term_") == 0)
                    << "Second term should start with 'term_' for segment doc " << i;

                totalDocs++;
            }
        }

        EXPECT_EQ(totalDocs, 10) << "Should have read exactly 10 docs across all segments";
    }
}
