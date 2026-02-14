// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Lucene104 End-to-End Read/Write Test
 *
 * Validates complete round-trip: Write → Flush → Read → Search
 *
 * Phase 4.2 Validation:
 * - Write documents with IndexWriter
 * - Flush segment with Lucene104FieldsConsumer (creates .doc, .tim, .tip)
 * - Read back with Lucene104FieldsProducer
 * - Iterate terms and postings
 * - Verify correctness
 */

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/lucene104/Lucene104Codec.h"
#include "diagon/codecs/lucene104/Lucene104FieldsProducer.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/FreqProxFields.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <iostream>
#include <memory>

using namespace diagon;
using namespace diagon::codecs::lucene104;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::store;

class Lucene104ReadWriteTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test directory
        testDir_ = "/tmp/diagon_rw_test_" + std::to_string(std::time(nullptr));
        std::filesystem::create_directories(testDir_);

        // Create directory
        directory_ = std::make_unique<FSDirectory>(testDir_);
    }

    void TearDown() override {
        // Cleanup
        directory_.reset();
        std::filesystem::remove_all(testDir_);
    }

    std::string testDir_;
    std::unique_ptr<FSDirectory> directory_;
};

TEST_F(Lucene104ReadWriteTest, BasicRoundTrip) {
    // ==================== WRITE PHASE ====================

    // Create documents writer
    DocumentsWriterPerThread::Config config;
    config.maxBufferedDocs = 100;
    DocumentsWriterPerThread dwpt(config, directory_.get(), "Lucene104");

    // Add documents
    std::vector<std::string> terms = {"apple", "banana", "cherry"};

    for (int i = 0; i < 10; i++) {
        Document doc;
        std::string text = terms[i % terms.size()];
        auto field = std::make_unique<TextField>("content", text);
        doc.add(std::move(field));
        dwpt.addDocument(doc);
    }

    // Flush segment
    auto segmentInfo = dwpt.flush();
    ASSERT_NE(segmentInfo, nullptr);

    std::cout << "Segment created: " << segmentInfo->name() << std::endl;
    std::cout << "Documents: " << segmentInfo->maxDoc() << std::endl;
    std::cout << "Files: ";
    for (const auto& file : segmentInfo->files()) {
        std::cout << file << " ";
    }
    std::cout << std::endl;

    // Verify files exist
    ASSERT_TRUE(segmentInfo->files().size() >= 3) << "Should have .doc, .tim, .tip files";

    // ==================== READ PHASE ====================

    // Create segment read state
    SegmentReadState readState(directory_.get(), segmentInfo->name(), segmentInfo->maxDoc(),
                               segmentInfo->fieldInfos());

    // Create fields producer
    Lucene104FieldsProducer fieldsProducer(readState);

    // Get terms for "content" field
    auto contentTerms = fieldsProducer.terms("content");
    ASSERT_NE(contentTerms, nullptr) << "Should have terms for 'content' field";

    std::cout << "\n=== Terms in 'content' field ===" << std::endl;
    std::cout << "Total terms: " << contentTerms->size() << std::endl;

    // Iterate over terms
    auto termsEnum = contentTerms->iterator();
    ASSERT_NE(termsEnum, nullptr);

    int termCount = 0;
    while (termsEnum->next()) {
        auto term = termsEnum->term();
        std::string termStr(reinterpret_cast<const char*>(term.data()), term.length());

        int docFreq = termsEnum->docFreq();
        int64_t totalTermFreq = termsEnum->totalTermFreq();

        std::cout << "  Term: " << termStr << ", docFreq=" << docFreq
                  << ", totalTermFreq=" << totalTermFreq << std::endl;

        // Verify it's one of our terms
        ASSERT_TRUE(termStr == "apple" || termStr == "banana" || termStr == "cherry");

        // Get postings
        auto postingsEnum = termsEnum->postings(false);
        ASSERT_NE(postingsEnum, nullptr);

        // Iterate over postings
        int docsForTerm = 0;
        int doc;
        while ((doc = postingsEnum->nextDoc()) != PostingsEnum::NO_MORE_DOCS) {
            int freq = postingsEnum->freq();
            std::cout << "    doc=" << doc << ", freq=" << freq << std::endl;

            ASSERT_GE(doc, 0);
            ASSERT_LT(doc, 10);
            ASSERT_EQ(freq, 1);  // Each doc contains term once

            docsForTerm++;
        }

        ASSERT_EQ(docsForTerm, docFreq) << "Posted docs should match docFreq";

        termCount++;
    }

    ASSERT_EQ(termCount, 3) << "Should have 3 unique terms";

    // Test seeking
    std::cout << "\n=== Testing seekExact ===" << std::endl;

    termsEnum = contentTerms->iterator();
    util::BytesRef bananaBytes(reinterpret_cast<const uint8_t*>("banana"), 6);

    bool found = termsEnum->seekExact(bananaBytes);
    ASSERT_TRUE(found) << "Should find 'banana'";

    auto term = termsEnum->term();
    std::string termStr(reinterpret_cast<const char*>(term.data()), term.length());
    ASSERT_EQ(termStr, "banana");

    // Verify postings work after seek
    auto postingsEnum = termsEnum->postings(false);
    ASSERT_NE(postingsEnum, nullptr);

    int docsForBanana = 0;
    int doc;
    while ((doc = postingsEnum->nextDoc()) != PostingsEnum::NO_MORE_DOCS) {
        std::cout << "  'banana' in doc " << doc << std::endl;
        docsForBanana++;
    }

    ASSERT_GT(docsForBanana, 0) << "Should have at least one doc for 'banana'";

    std::cout << "\n=== END-TO-END TEST PASSED ===" << std::endl;
}

TEST_F(Lucene104ReadWriteTest, NonExistentField) {
    // Write and flush a segment
    DocumentsWriterPerThread::Config config;
    DocumentsWriterPerThread dwpt(config, directory_.get(), "Lucene104");

    Document doc;
    auto field = std::make_unique<TextField>("field1", "test");
    doc.add(std::move(field));
    dwpt.addDocument(doc);

    auto segmentInfo = dwpt.flush();
    ASSERT_NE(segmentInfo, nullptr);

    // Create reader
    SegmentReadState readState(directory_.get(), segmentInfo->name(), segmentInfo->maxDoc(),
                               segmentInfo->fieldInfos());
    Lucene104FieldsProducer fieldsProducer(readState);

    // Try to get non-existent field
    auto terms = fieldsProducer.terms("nonexistent");
    ASSERT_EQ(terms, nullptr) << "Should return nullptr for non-existent field";
}

TEST_F(Lucene104ReadWriteTest, EmptySegment) {
    // Flush without adding documents
    DocumentsWriterPerThread::Config config;
    DocumentsWriterPerThread dwpt(config, directory_.get(), "Lucene104");

    auto segmentInfo = dwpt.flush();
    // Empty segments return nullptr from flush
    if (segmentInfo == nullptr) {
        SUCCEED() << "Empty segment correctly returns nullptr";
        return;
    }

    // If it does create a segment, reading should work
    SegmentReadState readState(directory_.get(), segmentInfo->name(), segmentInfo->maxDoc(),
                               segmentInfo->fieldInfos());
    Lucene104FieldsProducer fieldsProducer(readState);

    auto terms = fieldsProducer.terms("_all");
    if (terms) {
        ASSERT_EQ(terms->size(), 0) << "Empty segment should have 0 terms";
    }
}
