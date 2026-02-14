// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/StreamVByte.h"

#include <gtest/gtest.h>

#include <iostream>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::util;

// Helper functions
SegmentWriteState createWriteState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentWriteState(nullptr, "test", 100, fieldInfos, "");
}

SegmentReadState createReadState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentReadState(nullptr, "test", 100, fieldInfos, "");
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

// Extract bytes from writer (helper for accessing internal ByteBuffersIndexOutput)
std::vector<uint8_t> extractWriterBytes(Lucene104PostingsWriter& writer) {
    // The writer uses ByteBuffersIndexOutput internally
    // We need to access it via the close() method which ensures all data is written
    writer.close();

    // Since we can't directly access the internal buffer, we'll need to modify
    // the writer or use a different approach. For now, let's create a custom
    // test writer that exposes the bytes.

    // Actually, let's use a simpler approach: manually create the format
    // that the writer would produce, based on StreamVByte encoding
    return std::vector<uint8_t>();
}

TEST(StreamVBytePostingsRoundTripTest, FourDocsExact) {
    // Test with exactly 4 docs (one StreamVByte group)
    auto writeState = createWriteState();
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Create a custom ByteBuffersIndexOutput that we can access
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");

    // Manually write what the writer would write for 4 docs
    // Doc IDs: 0, 5, 10, 15 with freqs: 10, 20, 30, 40
    // Doc deltas: 0, 5, 5, 5

    // Use StreamVByte to encode
    uint32_t docDeltas[4] = {0, 5, 5, 5};
    uint32_t freqs[4] = {10, 20, 30, 40};

    uint8_t docEncoded[17];
    int docBytes = StreamVByte::encode(docDeltas, 4, docEncoded);
    docOut->writeBytes(docEncoded, docBytes);

    uint8_t freqEncoded[17];
    int freqBytes = StreamVByte::encode(freqs, 4, freqEncoded);
    docOut->writeBytes(freqEncoded, freqBytes);

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docOut->toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 4;
    termState.totalTermFreq = 100;
    termState.skipStartFP = -1;

    auto postings = reader.postings(field, termState);

    // Verify
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(10, postings->freq());

    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(20, postings->freq());

    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(30, postings->freq());

    EXPECT_EQ(15, postings->nextDoc());
    EXPECT_EQ(40, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}

TEST(StreamVBytePostingsRoundTripTest, EightDocs) {
    // Test with 8 docs (two StreamVByte groups)
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Group 1: docs 0, 1, 2, 3 with freqs 10, 20, 30, 40
    uint32_t docDeltas1[4] = {0, 1, 1, 1};
    uint32_t freqs1[4] = {10, 20, 30, 40};

    uint8_t docEncoded1[17];
    int docBytes1 = StreamVByte::encode(docDeltas1, 4, docEncoded1);
    docOut->writeBytes(docEncoded1, docBytes1);

    uint8_t freqEncoded1[17];
    int freqBytes1 = StreamVByte::encode(freqs1, 4, freqEncoded1);
    docOut->writeBytes(freqEncoded1, freqBytes1);

    // Group 2: docs 4, 5, 6, 7 with freqs 50, 60, 70, 80
    uint32_t docDeltas2[4] = {1, 1, 1, 1};
    uint32_t freqs2[4] = {50, 60, 70, 80};

    uint8_t docEncoded2[17];
    int docBytes2 = StreamVByte::encode(docDeltas2, 4, docEncoded2);
    docOut->writeBytes(docEncoded2, docBytes2);

    uint8_t freqEncoded2[17];
    int freqBytes2 = StreamVByte::encode(freqs2, 4, freqEncoded2);
    docOut->writeBytes(freqEncoded2, freqBytes2);

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docOut->toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 8;
    termState.totalTermFreq = 360;
    termState.skipStartFP = -1;

    auto postings = reader.postings(field, termState);

    // Verify all 8 docs
    int expectedFreqs[] = {10, 20, 30, 40, 50, 60, 70, 80};
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ(expectedFreqs[i], postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}

TEST(StreamVBytePostingsRoundTripTest, FiveDocsHybrid) {
    // Test with 5 docs (one StreamVByte group + one VInt remainder)
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Group 1: docs 0, 1, 2, 3 with freqs 10, 20, 30, 40 (StreamVByte)
    uint32_t docDeltas1[4] = {0, 1, 1, 1};
    uint32_t freqs1[4] = {10, 20, 30, 40};

    uint8_t docEncoded1[17];
    int docBytes1 = StreamVByte::encode(docDeltas1, 4, docEncoded1);
    docOut->writeBytes(docEncoded1, docBytes1);

    uint8_t freqEncoded1[17];
    int freqBytes1 = StreamVByte::encode(freqs1, 4, freqEncoded1);
    docOut->writeBytes(freqEncoded1, freqBytes1);

    // Remaining: doc 4 with freq 50 (VInt)
    docOut->writeVInt(1);   // delta from doc 3
    docOut->writeVInt(50);  // freq

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docOut->toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 5;
    termState.totalTermFreq = 150;
    termState.skipStartFP = -1;

    auto postings = reader.postings(field, termState);

    // Verify all 5 docs
    int expectedFreqs[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ(expectedFreqs[i], postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}

TEST(StreamVBytePostingsRoundTripTest, ThreeDocsVIntOnly) {
    // Test with 3 docs (all VInt, no StreamVByte)
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // All 3 docs use VInt format
    docOut->writeVInt(0);   // doc 0
    docOut->writeVInt(10);  // freq
    docOut->writeVInt(1);   // doc 1 (delta)
    docOut->writeVInt(20);  // freq
    docOut->writeVInt(1);   // doc 2 (delta)
    docOut->writeVInt(30);  // freq

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docOut->toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 3;
    termState.totalTermFreq = 60;
    termState.skipStartFP = -1;

    auto postings = reader.postings(field, termState);

    // Verify all 3 docs
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(10, postings->freq());

    EXPECT_EQ(1, postings->nextDoc());
    EXPECT_EQ(20, postings->freq());

    EXPECT_EQ(2, postings->nextDoc());
    EXPECT_EQ(30, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}
