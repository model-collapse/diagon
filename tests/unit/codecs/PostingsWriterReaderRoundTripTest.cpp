// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

// Comprehensive round-trip tests: Write with Lucene104PostingsWriter, read with
// Lucene104PostingsReader

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"

#include <gtest/gtest.h>

#include <iostream>
#include <random>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;

// ==================== Helper Functions ====================

SegmentWriteState createWriteState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentWriteState(nullptr, "test", 100000, fieldInfos, "");
}

SegmentReadState createReadState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentReadState(nullptr, "test", 100000, fieldInfos, "");
}

FieldInfo createField(const std::string& name, IndexOptions options) {
    FieldInfo field;
    field.name = name;
    field.number = 0;
    field.indexOptions = options;
    field.storeTermVector = false;
    field.omitNorms = false;
    field.storePayloads = false;
    field.docValuesType = DocValuesType::NONE;
    field.dvGen = -1;
    return field;
}

// ==================== Round-Trip Tests ====================

TEST(PostingsWriterReaderRoundTripTest, ThreeDocsVIntOnly) {
    // Test with 3 docs (< 4, so VInt fallback only)
    std::cout << "\n=== Testing 3 docs (VInt only) ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();
    writer.startDoc(0, 10);
    writer.startDoc(5, 20);
    writer.startDoc(10, 30);
    TermState termState = writer.finishTerm();

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify docs
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(10, postings->freq());

    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(20, postings->freq());

    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(30, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, FourDocsStreamVByte) {
    // Test with exactly 4 docs (one StreamVByte group)
    std::cout << "\n=== Testing 4 docs (StreamVByte) ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();
    writer.startDoc(0, 10);
    writer.startDoc(5, 20);
    writer.startDoc(10, 30);
    writer.startDoc(15, 40);
    TermState termState = writer.finishTerm();

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify all 4 docs
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(10, postings->freq());

    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(20, postings->freq());

    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(30, postings->freq());

    EXPECT_EQ(15, postings->nextDoc());
    EXPECT_EQ(40, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, FiveDocsHybrid) {
    // Test with 5 docs (4 StreamVByte + 1 VInt)
    std::cout << "\n=== Testing 5 docs (Hybrid: 4 StreamVByte + 1 VInt) ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();
    writer.startDoc(0, 10);
    writer.startDoc(1, 20);
    writer.startDoc(2, 30);
    writer.startDoc(3, 40);
    writer.startDoc(4, 50);
    TermState termState = writer.finishTerm();

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify all 5 docs
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ((i + 1) * 10, postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, EightDocsDoubleStreamVByte) {
    // Test with 8 docs (two StreamVByte groups)
    std::cout << "\n=== Testing 8 docs (2 StreamVByte groups) ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();
    for (int i = 0; i < 8; ++i) {
        writer.startDoc(i, (i + 1) * 10);
    }
    TermState termState = writer.finishTerm();

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify all 8 docs
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ((i + 1) * 10, postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, ThousandDocsLarge) {
    // Test with 1000 docs (250 StreamVByte groups)
    std::cout << "\n=== Testing 1000 docs (250 StreamVByte groups) ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();

    int64_t expectedTotalFreq = 0;
    for (int i = 0; i < 1000; ++i) {
        int freq = (i % 10) + 1;
        writer.startDoc(i, freq);
        expectedTotalFreq += freq;
    }

    TermState termState = writer.finishTerm();
    EXPECT_EQ(expectedTotalFreq, termState.totalTermFreq);

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify all 1000 docs
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ((i % 10) + 1, postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, DocsOnlyMode) {
    // Test DOCS_ONLY mode (no frequencies)
    std::cout << "\n=== Testing DOCS_ONLY mode ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("id", IndexOptions::DOCS);

    writer.setField(field);
    writer.startTerm();
    writer.startDoc(0, 1);  // freq ignored
    writer.startDoc(5, 1);
    writer.startDoc(10, 1);
    writer.startDoc(15, 1);
    TermState termState = writer.finishTerm();

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    EXPECT_EQ(-1, termState.totalTermFreq);  // Not tracked for DOCS_ONLY

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify docs (freq should default to 1)
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(15, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, RandomDocIDs) {
    // Test with random doc IDs and frequencies
    std::cout << "\n=== Testing 100 docs with random IDs/freqs ===" << std::endl;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> deltaDist(1, 100);
    std::uniform_int_distribution<int> freqDist(1, 50);

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();

    std::vector<int> expectedDocs;
    std::vector<int> expectedFreqs;

    int currentDoc = 0;
    int64_t expectedTotalFreq = 0;
    for (int i = 0; i < 100; ++i) {
        currentDoc += deltaDist(rng);
        int freq = freqDist(rng);
        writer.startDoc(currentDoc, freq);
        expectedDocs.push_back(currentDoc);
        expectedFreqs.push_back(freq);
        expectedTotalFreq += freq;
    }

    TermState termState = writer.finishTerm();
    EXPECT_EQ(expectedTotalFreq, termState.totalTermFreq);

    std::cout << "Writer: docFreq=" << termState.docFreq
              << ", totalTermFreq=" << termState.totalTermFreq << std::endl;

    // Get encoded bytes
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    auto postings = reader.postings(field, termState);

    // Verify all docs
    for (size_t i = 0; i < expectedDocs.size(); ++i) {
        EXPECT_EQ(expectedDocs[i], postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ(expectedFreqs[i], postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
    std::cout << "✓ Round-trip successful" << std::endl;
}

TEST(PostingsWriterReaderRoundTripTest, MultipleTerms) {
    // Test writing and reading multiple terms
    std::cout << "\n=== Testing multiple terms ===" << std::endl;

    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);

    // Term 1: "hello"
    writer.startTerm();
    writer.startDoc(0, 1);
    writer.startDoc(5, 2);
    writer.startDoc(10, 1);
    TermState term1State = writer.finishTerm();

    // Term 2: "world"
    writer.startTerm();
    writer.startDoc(2, 3);
    writer.startDoc(7, 1);
    writer.startDoc(12, 2);
    writer.startDoc(20, 1);
    TermState term2State = writer.finishTerm();

    std::cout << "Term 1: docFreq=" << term1State.docFreq
              << ", totalTermFreq=" << term1State.totalTermFreq
              << ", startFP=" << term1State.docStartFP << std::endl;

    std::cout << "Term 2: docFreq=" << term2State.docFreq
              << ", totalTermFreq=" << term2State.totalTermFreq
              << ", startFP=" << term2State.docStartFP << std::endl;

    // Get encoded bytes (contains both terms)
    std::vector<uint8_t> bytes = writer.getBytes();
    std::cout << "Encoded " << bytes.size() << " bytes for both terms" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", bytes));

    // Read term 1
    auto postings1 = reader.postings(field, term1State);
    EXPECT_EQ(0, postings1->nextDoc());
    EXPECT_EQ(1, postings1->freq());
    EXPECT_EQ(5, postings1->nextDoc());
    EXPECT_EQ(2, postings1->freq());
    EXPECT_EQ(10, postings1->nextDoc());
    EXPECT_EQ(1, postings1->freq());
    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings1->nextDoc());

    // Read term 2
    auto postings2 = reader.postings(field, term2State);
    EXPECT_EQ(2, postings2->nextDoc());
    EXPECT_EQ(3, postings2->freq());
    EXPECT_EQ(7, postings2->nextDoc());
    EXPECT_EQ(1, postings2->freq());
    EXPECT_EQ(12, postings2->nextDoc());
    EXPECT_EQ(2, postings2->freq());
    EXPECT_EQ(20, postings2->nextDoc());
    EXPECT_EQ(1, postings2->freq());
    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings2->nextDoc());

    std::cout << "✓ Round-trip successful for both terms" << std::endl;
}
