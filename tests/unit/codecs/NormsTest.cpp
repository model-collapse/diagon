// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104NormsReader.h"
#include "diagon/codecs/lucene104/Lucene104NormsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/search/DocIdSetIterator.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <vector>

using namespace diagon;
using namespace diagon::codecs;
using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::search;

namespace fs = std::filesystem;

class NormsTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_norms_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);

        directory_ = FSDirectory::open(testDir_.string());
        segmentInfo_ = std::make_shared<SegmentInfo>("_0", 100, "Lucene104");

        // Create FieldInfos for SegmentWriteState/SegmentReadState
        std::vector<FieldInfo> infos;
        FieldInfo fi;
        fi.name = "body";
        fi.number = 0;
        fi.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
        fi.omitNorms = false;
        infos.push_back(fi);
        fieldInfos_ = std::make_unique<FieldInfos>(std::move(infos));
    }

    void TearDown() override {
        directory_.reset();
        fs::remove_all(testDir_);
    }

    /**
     * Create simple norms producer for testing
     * Returns norms based on doc ID (for testing)
     */
    class TestNormsProducer : public NormsProducer {
    public:
        explicit TestNormsProducer(std::vector<int8_t> norms)
            : norms_(std::move(norms)) {}

        std::unique_ptr<NumericDocValues> getNorms(const FieldInfo& field) override {
            return std::make_unique<TestNormsValues>(norms_);
        }

        void checkIntegrity() override {}
        void close() override {}

    private:
        class TestNormsValues : public NumericDocValues {
        public:
            explicit TestNormsValues(const std::vector<int8_t>& norms)
                : norms_(norms)
                , docID_(-1) {}

            bool advanceExact(int target) override {
                docID_ = target;
                return target >= 0 && target < static_cast<int>(norms_.size());
            }

            int64_t longValue() const override {
                if (docID_ < 0 || docID_ >= static_cast<int>(norms_.size())) {
                    return 0;
                }
                return static_cast<int64_t>(norms_[docID_]);
            }

            int docID() const override { return docID_; }

            int nextDoc() override {
                if (docID_ + 1 < static_cast<int>(norms_.size())) {
                    docID_++;
                    return docID_;
                }
                docID_ = NO_MORE_DOCS;
                return NO_MORE_DOCS;
            }

            int advance(int target) override {
                if (target >= static_cast<int>(norms_.size())) {
                    docID_ = NO_MORE_DOCS;
                    return NO_MORE_DOCS;
                }
                docID_ = target;
                return docID_;
            }

            int64_t cost() const override { return norms_.size(); }

        private:
            std::vector<int8_t> norms_;
            int docID_;
        };

        std::vector<int8_t> norms_;
    };

    fs::path testDir_;
    std::unique_ptr<FSDirectory> directory_;
    std::shared_ptr<SegmentInfo> segmentInfo_;
    std::unique_ptr<FieldInfos> fieldInfos_;
};

// ==================== Test 1: Write and Read Norms ====================

TEST_F(NormsTest, WriteAndReadNorms) {
    // Create field info
    FieldInfo field;
    field.name = "body";
    field.number = 0;
    field.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    field.omitNorms = false;

    // Create norms data (100 documents with varying norms)
    std::vector<int8_t> expectedNorms;
    for (int i = 0; i < 100; i++) {
        expectedNorms.push_back(static_cast<int8_t>(127 - i));
    }

    // Write norms
    {
        index::SegmentWriteState writeState(directory_.get(), segmentInfo_->name(), 100,
                                            *fieldInfos_);
        Lucene104NormsWriter writer(writeState);
        TestNormsProducer producer(expectedNorms);
        writer.addNormsField(field, producer);
        writer.close();
    }

    // Read norms
    {
        index::SegmentReadState readState(directory_.get(), segmentInfo_->name(), 100,
                                          *fieldInfos_);
        Lucene104NormsReader reader(readState);
        auto normsIter = reader.getNorms(field);
        ASSERT_TRUE(normsIter);

        // Verify norms values
        for (int doc = 0; doc < 100; doc++) {
            ASSERT_TRUE(normsIter->advanceExact(doc));
            int64_t value = normsIter->longValue();
            EXPECT_EQ(expectedNorms[doc], static_cast<int8_t>(value)) << "Mismatch at doc " << doc;
        }

        reader.close();
    }
}

// ==================== Test 2: Empty Norms ====================

TEST_F(NormsTest, EmptyNorms) {
    FieldInfo field;
    field.name = "empty";
    field.number = 1;
    field.indexOptions = IndexOptions::DOCS_AND_FREQS;
    field.omitNorms = false;

    // Create empty norms (all zeros)
    std::vector<int8_t> expectedNorms(100, 0);

    // Write and read
    {
        index::SegmentWriteState writeState(directory_.get(), segmentInfo_->name(), 100,
                                            *fieldInfos_);
        Lucene104NormsWriter writer(writeState);
        TestNormsProducer producer(expectedNorms);
        writer.addNormsField(field, producer);
        writer.close();
    }

    {
        index::SegmentReadState readState(directory_.get(), segmentInfo_->name(), 100,
                                          *fieldInfos_);
        Lucene104NormsReader reader(readState);
        auto normsIter = reader.getNorms(field);
        ASSERT_TRUE(normsIter);

        // Verify all zeros
        for (int doc = 0; doc < 100; doc++) {
            ASSERT_TRUE(normsIter->advanceExact(doc));
            EXPECT_EQ(0, normsIter->longValue());
        }

        reader.close();
    }
}

// ==================== Test 3: Maximum Norms ====================

TEST_F(NormsTest, MaximumNorms) {
    FieldInfo field;
    field.name = "maxnorms";
    field.number = 2;
    field.indexOptions = IndexOptions::DOCS;
    field.omitNorms = false;

    // All maximum norms (127)
    std::vector<int8_t> expectedNorms(100, 127);

    // Write and read
    {
        index::SegmentWriteState writeState(directory_.get(), segmentInfo_->name(), 100,
                                            *fieldInfos_);
        Lucene104NormsWriter writer(writeState);
        TestNormsProducer producer(expectedNorms);
        writer.addNormsField(field, producer);
        writer.close();
    }

    {
        index::SegmentReadState readState(directory_.get(), segmentInfo_->name(), 100,
                                          *fieldInfos_);
        Lucene104NormsReader reader(readState);
        auto normsIter = reader.getNorms(field);
        ASSERT_TRUE(normsIter);

        // Verify all 127
        for (int doc = 0; doc < 100; doc++) {
            ASSERT_TRUE(normsIter->advanceExact(doc));
            EXPECT_EQ(127, normsIter->longValue());
        }

        reader.close();
    }
}

// ==================== Test 4: Negative Norms ====================

TEST_F(NormsTest, NegativeNorms) {
    FieldInfo field;
    field.name = "negative";
    field.number = 3;
    field.indexOptions = IndexOptions::DOCS_AND_FREQS;
    field.omitNorms = false;

    // Mix of positive and negative norms
    std::vector<int8_t> expectedNorms;
    for (int i = 0; i < 100; i++) {
        expectedNorms.push_back(static_cast<int8_t>(i - 50));
    }

    // Write and read
    {
        index::SegmentWriteState writeState(directory_.get(), segmentInfo_->name(), 100,
                                            *fieldInfos_);
        Lucene104NormsWriter writer(writeState);
        TestNormsProducer producer(expectedNorms);
        writer.addNormsField(field, producer);
        writer.close();
    }

    {
        index::SegmentReadState readState(directory_.get(), segmentInfo_->name(), 100,
                                          *fieldInfos_);
        Lucene104NormsReader reader(readState);
        auto normsIter = reader.getNorms(field);
        ASSERT_TRUE(normsIter);

        // Verify values
        for (int doc = 0; doc < 100; doc++) {
            ASSERT_TRUE(normsIter->advanceExact(doc));
            int64_t value = normsIter->longValue();
            EXPECT_EQ(expectedNorms[doc], static_cast<int8_t>(value));
        }

        reader.close();
    }
}

// ==================== Test 5: Norms Iterator ====================

TEST_F(NormsTest, NormsIterator) {
    FieldInfo field;
    field.name = "iterable";
    field.number = 4;
    field.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    field.omitNorms = false;

    std::vector<int8_t> expectedNorms;
    for (int i = 0; i < 100; i++) {
        expectedNorms.push_back(static_cast<int8_t>(i % 128));
    }

    // Write and read
    {
        index::SegmentWriteState writeState(directory_.get(), segmentInfo_->name(), 100,
                                            *fieldInfos_);
        Lucene104NormsWriter writer(writeState);
        TestNormsProducer producer(expectedNorms);
        writer.addNormsField(field, producer);
        writer.close();
    }

    {
        index::SegmentReadState readState(directory_.get(), segmentInfo_->name(), 100,
                                          *fieldInfos_);
        Lucene104NormsReader reader(readState);
        auto normsIter = reader.getNorms(field);
        ASSERT_TRUE(normsIter);

        // Test nextDoc() iteration
        int count = 0;
        int doc = normsIter->nextDoc();
        while (doc != DocIdSetIterator::NO_MORE_DOCS) {
            int64_t value = normsIter->longValue();
            EXPECT_EQ(expectedNorms[doc], static_cast<int8_t>(value));
            count++;
            doc = normsIter->nextDoc();
        }
        EXPECT_EQ(100, count);

        reader.close();
    }
}
