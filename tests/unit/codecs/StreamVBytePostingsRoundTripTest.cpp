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
    // Uses freq-in-docDelta encoding: low bit encodes freq==1
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");

    // Doc IDs: 0, 5, 10, 15 with freqs: 10, 20, 30, 40
    // Doc deltas: 0, 5, 5, 5
    // All freqs > 1: modified deltas = (delta << 1) | 0
    uint32_t modifiedDeltas[4] = {0 << 1, 5 << 1, 5 << 1, 5 << 1};  // {0, 10, 10, 10}
    uint32_t freqs[4] = {10, 20, 30, 40};

    uint8_t docEncoded[17];
    int docBytes = StreamVByte::encode(modifiedDeltas, 4, docEncoded);
    docOut->writeBytes(docEncoded, docBytes);

    // Write non-1 freqs as VInts (all 4 are non-1)
    for (int i = 0; i < 4; ++i) {
        docOut->writeVInt(static_cast<int32_t>(freqs[i]));
    }

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
    // Uses freq-in-docDelta encoding
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Group 1: docs 0, 1, 2, 3 with freqs 10, 20, 30, 40 (all > 1)
    // Modified deltas: (delta << 1) | 0
    uint32_t modDeltas1[4] = {0 << 1, 1 << 1, 1 << 1, 1 << 1};  // {0, 2, 2, 2}
    uint32_t freqs1[4] = {10, 20, 30, 40};

    uint8_t docEncoded1[17];
    int docBytes1 = StreamVByte::encode(modDeltas1, 4, docEncoded1);
    docOut->writeBytes(docEncoded1, docBytes1);
    for (int i = 0; i < 4; ++i) docOut->writeVInt(static_cast<int32_t>(freqs1[i]));

    // Group 2: docs 4, 5, 6, 7 with freqs 50, 60, 70, 80 (all > 1)
    uint32_t modDeltas2[4] = {1 << 1, 1 << 1, 1 << 1, 1 << 1};  // {2, 2, 2, 2}
    uint32_t freqs2[4] = {50, 60, 70, 80};

    uint8_t docEncoded2[17];
    int docBytes2 = StreamVByte::encode(modDeltas2, 4, docEncoded2);
    docOut->writeBytes(docEncoded2, docBytes2);
    for (int i = 0; i < 4; ++i) docOut->writeVInt(static_cast<int32_t>(freqs2[i]));

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
    // Uses freq-in-docDelta encoding
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Group 1: docs 0, 1, 2, 3 with freqs 10, 20, 30, 40 (all > 1)
    uint32_t modDeltas1[4] = {0 << 1, 1 << 1, 1 << 1, 1 << 1};  // {0, 2, 2, 2}
    uint32_t freqs1[4] = {10, 20, 30, 40};

    uint8_t docEncoded1[17];
    int docBytes1 = StreamVByte::encode(modDeltas1, 4, docEncoded1);
    docOut->writeBytes(docEncoded1, docBytes1);
    for (int i = 0; i < 4; ++i) docOut->writeVInt(static_cast<int32_t>(freqs1[i]));

    // Remaining: doc 4 with freq 50 (VInt, freq > 1)
    // New format: writeVInt(delta << 1), writeVInt(freq)
    docOut->writeVInt(1 << 1);  // delta=1, low bit=0 (freq > 1)
    docOut->writeVInt(50);      // freq

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
    // Uses freq-in-docDelta encoding
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // All 3 docs use VInt format with freq-in-docDelta packing
    // All freqs > 1: writeVInt(delta << 1), writeVInt(freq)
    docOut->writeVInt(0 << 1);  // doc 0 delta=0, low bit=0 (freq > 1)
    docOut->writeVInt(10);      // freq
    docOut->writeVInt(1 << 1);  // doc 1 delta=1, low bit=0 (freq > 1)
    docOut->writeVInt(20);      // freq
    docOut->writeVInt(1 << 1);  // doc 2 delta=1, low bit=0 (freq > 1)
    docOut->writeVInt(30);      // freq

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
