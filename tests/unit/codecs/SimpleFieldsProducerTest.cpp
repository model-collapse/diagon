// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SimpleFieldsConsumer.h"
#include "diagon/codecs/SimpleFieldsProducer.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/index/SegmentWriteState.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <unordered_map>

using namespace diagon;
using namespace diagon::codecs;
using namespace diagon::store;
using namespace diagon::index;

namespace fs = std::filesystem;

class SimpleFieldsProducerTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_fields_producer_test";
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

    // Helper: Write test data
    void writeTestData(
        const std::string& segmentName,
        const std::unordered_map<std::string, std::vector<int>>& terms
    ) {
        // Create minimal FieldInfos with body field
        FieldInfo bodyField{
            .name = "body",
            .number = 0,
            .indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS
        };
        FieldInfos fieldInfos({bodyField});

        index::SegmentWriteState state(dir.get(), segmentName, 1000, fieldInfos, "");

        SimpleFieldsConsumer consumer(state);
        consumer.writeField("body", terms);
        consumer.close();
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> dir;
};

// ==================== Basic Read Tests ====================

TEST_F(SimpleFieldsProducerTest, ReadSimpleData) {
    // Write data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["hello"] = {0, 1, 1, 1};  // doc 0 freq 1, doc 1 freq 1
    terms["world"] = {0, 2};         // doc 0 freq 2

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");

    // Check size
    EXPECT_EQ(producer.size(), 2);

    // Get terms
    auto termsObj = producer.terms();
    EXPECT_NE(termsObj, nullptr);
    EXPECT_EQ(termsObj->size(), 2);
}

TEST_F(SimpleFieldsProducerTest, IterateTerms) {
    // Write sorted terms
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["apple"] = {0, 1};
    terms["banana"] = {1, 2};
    terms["cherry"] = {2, 1};

    writeTestData("_0", terms);

    // Read and iterate
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Should iterate in sorted order
    ASSERT_TRUE(termsEnum->next());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "apple");

    ASSERT_TRUE(termsEnum->next());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "banana");

    ASSERT_TRUE(termsEnum->next());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "cherry");

    EXPECT_FALSE(termsEnum->next());  // No more terms
}

TEST_F(SimpleFieldsProducerTest, ReadPostings) {
    // Write test data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["test"] = {0, 1, 5, 2, 10, 3};  // docs: 0(freq=1), 5(freq=2), 10(freq=3)

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Get to "test" term
    ASSERT_TRUE(termsEnum->next());

    // Check term stats
    EXPECT_EQ(termsEnum->docFreq(), 3);  // 3 documents
    EXPECT_EQ(termsEnum->totalTermFreq(), 1 + 2 + 3);  // Total freq

    // Get postings
    auto postings = termsEnum->postings();
    ASSERT_NE(postings, nullptr);

    // Iterate postings
    EXPECT_EQ(postings->nextDoc(), 0);
    EXPECT_EQ(postings->docID(), 0);
    EXPECT_EQ(postings->freq(), 1);

    EXPECT_EQ(postings->nextDoc(), 5);
    EXPECT_EQ(postings->docID(), 5);
    EXPECT_EQ(postings->freq(), 2);

    EXPECT_EQ(postings->nextDoc(), 10);
    EXPECT_EQ(postings->docID(), 10);
    EXPECT_EQ(postings->freq(), 3);

    EXPECT_EQ(postings->nextDoc(), PostingsEnum::NO_MORE_DOCS);
}

// ==================== Seek Tests ====================

TEST_F(SimpleFieldsProducerTest, SeekExactFound) {
    // Write data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["apple"] = {0, 1};
    terms["banana"] = {1, 1};
    terms["cherry"] = {2, 1};

    writeTestData("_0", terms);

    // Read and seek
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Seek to "banana"
    util::BytesRef target(reinterpret_cast<const uint8_t*>("banana"), 6);
    EXPECT_TRUE(termsEnum->seekExact(target));

    // Verify positioned correctly
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "banana");
    EXPECT_EQ(termsEnum->docFreq(), 1);
}

TEST_F(SimpleFieldsProducerTest, SeekExactNotFound) {
    // Write data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["apple"] = {0, 1};
    terms["cherry"] = {2, 1};

    writeTestData("_0", terms);

    // Read and seek
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Seek to "banana" (not present)
    util::BytesRef target(reinterpret_cast<const uint8_t*>("banana"), 6);
    EXPECT_FALSE(termsEnum->seekExact(target));
}

TEST_F(SimpleFieldsProducerTest, SeekCeilFound) {
    // Write data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["apple"] = {0, 1};
    terms["banana"] = {1, 1};
    terms["cherry"] = {2, 1};

    writeTestData("_0", terms);

    // Read and seek
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Seek ceil to "banana" (exists)
    util::BytesRef target(reinterpret_cast<const uint8_t*>("banana"), 6);
    auto status = termsEnum->seekCeil(target);
    EXPECT_EQ(status, TermsEnum::SeekStatus::FOUND);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "banana");
}

TEST_F(SimpleFieldsProducerTest, SeekCeilNotFoundButPositioned) {
    // Write data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["apple"] = {0, 1};
    terms["cherry"] = {2, 1};

    writeTestData("_0", terms);

    // Read and seek
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Seek ceil to "banana" (not present, should position at "cherry")
    util::BytesRef target(reinterpret_cast<const uint8_t*>("banana"), 6);
    auto status = termsEnum->seekCeil(target);
    EXPECT_EQ(status, TermsEnum::SeekStatus::NOT_FOUND);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "cherry");
}

TEST_F(SimpleFieldsProducerTest, SeekCeilEnd) {
    // Write data
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["apple"] = {0, 1};
    terms["banana"] = {1, 1};

    writeTestData("_0", terms);

    // Read and seek
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Seek ceil past last term
    util::BytesRef target(reinterpret_cast<const uint8_t*>("zebra"), 5);
    auto status = termsEnum->seekCeil(target);
    EXPECT_EQ(status, TermsEnum::SeekStatus::END);
}

// ==================== Postings Advance Tests ====================

TEST_F(SimpleFieldsProducerTest, PostingsAdvance) {
    // Write data with gaps
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["test"] = {0, 1, 5, 1, 10, 1, 20, 1, 30, 1};

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();
    ASSERT_TRUE(termsEnum->next());

    auto postings = termsEnum->postings();

    // Advance to 15
    EXPECT_EQ(postings->advance(15), 20);
    EXPECT_EQ(postings->docID(), 20);

    // Advance to 25
    EXPECT_EQ(postings->advance(25), 30);
    EXPECT_EQ(postings->docID(), 30);

    // Advance past end
    EXPECT_EQ(postings->advance(50), PostingsEnum::NO_MORE_DOCS);
}

// ==================== Multiple Terms Tests ====================

TEST_F(SimpleFieldsProducerTest, ManyTerms) {
    // Write many terms
    std::unordered_map<std::string, std::vector<int>> terms;
    for (int i = 0; i < 100; i++) {
        std::string term = "term" + std::to_string(i);
        terms[term] = {i, 1};
    }

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    EXPECT_EQ(producer.size(), 100);

    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    // Count terms
    int count = 0;
    while (termsEnum->next()) {
        count++;
    }
    EXPECT_EQ(count, 100);
}

TEST_F(SimpleFieldsProducerTest, LargePostingsList) {
    // Write term with many postings
    std::unordered_map<std::string, std::vector<int>> terms;
    std::vector<int> postings;
    for (int i = 0; i < 1000; i++) {
        postings.push_back(i);
        postings.push_back(1);  // freq
    }
    terms["common"] = postings;

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();
    ASSERT_TRUE(termsEnum->next());

    EXPECT_EQ(termsEnum->docFreq(), 1000);

    auto postingsEnum = termsEnum->postings();
    int count = 0;
    while (postingsEnum->nextDoc() != PostingsEnum::NO_MORE_DOCS) {
        count++;
    }
    EXPECT_EQ(count, 1000);
}

// ==================== Edge Cases ====================

TEST_F(SimpleFieldsProducerTest, EmptyTerms) {
    // Write empty terms
    std::unordered_map<std::string, std::vector<int>> terms;
    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    EXPECT_EQ(producer.size(), 0);

    auto termsObj = producer.terms();
    EXPECT_EQ(termsObj->size(), 0);

    auto termsEnum = termsObj->iterator();
    EXPECT_FALSE(termsEnum->next());
}

TEST_F(SimpleFieldsProducerTest, SingleTerm) {
    // Write single term
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["only"] = {42, 7};

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    EXPECT_EQ(producer.size(), 1);

    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();

    ASSERT_TRUE(termsEnum->next());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(termsEnum->term().bytes().data()),
                         termsEnum->term().length()), "only");
    EXPECT_EQ(termsEnum->docFreq(), 1);

    auto postings = termsEnum->postings();
    EXPECT_EQ(postings->nextDoc(), 42);
    EXPECT_EQ(postings->freq(), 7);
    EXPECT_EQ(postings->nextDoc(), PostingsEnum::NO_MORE_DOCS);

    EXPECT_FALSE(termsEnum->next());
}

TEST_F(SimpleFieldsProducerTest, HighFrequencies) {
    // Write term with high frequency
    std::unordered_map<std::string, std::vector<int>> terms;
    terms["frequent"] = {0, 1000, 1, 999};

    writeTestData("_0", terms);

    // Read back
    SimpleFieldsProducer producer(*dir, "_0", "body");
    auto termsObj = producer.terms();
    auto termsEnum = termsObj->iterator();
    ASSERT_TRUE(termsEnum->next());

    EXPECT_EQ(termsEnum->totalTermFreq(), 1000 + 999);

    auto postings = termsEnum->postings();
    EXPECT_EQ(postings->nextDoc(), 0);
    EXPECT_EQ(postings->freq(), 1000);

    EXPECT_EQ(postings->nextDoc(), 1);
    EXPECT_EQ(postings->freq(), 999);
}
