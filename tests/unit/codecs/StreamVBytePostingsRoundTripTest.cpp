// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/BitPacking.h"

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

TEST(BitPackPostingsRoundTripTest, FourDocsVIntTail) {
    // Test with 4 docs (< 128, all VInt tail)
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");

    // Doc IDs: 0, 5, 10, 15 with freqs: 10, 20, 30, 40 (all > 1)
    // VInt format: writeVInt(delta << 1), writeVInt(freq)
    docOut->writeVInt(0 << 1);
    docOut->writeVInt(10);
    docOut->writeVInt(5 << 1);
    docOut->writeVInt(20);
    docOut->writeVInt(5 << 1);
    docOut->writeVInt(30);
    docOut->writeVInt(5 << 1);
    docOut->writeVInt(40);

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

TEST(BitPackPostingsRoundTripTest, OneBitPackBlockPlusVIntTail) {
    // Test with 130 docs (one 128-doc BitPack block + 2 VInt tail)
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Build 128-doc block: docs 0..127, all freq=1
    uint32_t modDeltas[128];
    for (int i = 0; i < 128; ++i) {
        uint32_t delta = (i == 0) ? 0 : 1;
        modDeltas[i] = (delta << 1) | 1;  // freq=1 → low bit set
    }

    uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
    int encodedBytes = BitPacking::encode(modDeltas, 128, encoded);
    docOut->writeBytes(encoded, encodedBytes);
    // No non-1 freqs to write

    // VInt tail: 2 more docs (128, 129) with freq > 1
    docOut->writeVInt(1 << 1);   // delta=1, low bit=0 (freq > 1)
    docOut->writeVInt(50);       // freq=50
    docOut->writeVInt(1 << 1);   // delta=1, low bit=0 (freq > 1)
    docOut->writeVInt(60);       // freq=60

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docOut->toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 130;
    termState.totalTermFreq = 128 + 50 + 60;  // 128 × 1 + 50 + 60
    termState.skipStartFP = -1;

    auto postings = reader.postings(field, termState);

    // Verify first 128 docs (from BitPack block)
    for (int i = 0; i < 128; i++) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ(1, postings->freq()) << "Freq for doc " << i;
    }

    // Verify VInt tail docs
    EXPECT_EQ(128, postings->nextDoc());
    EXPECT_EQ(50, postings->freq());

    EXPECT_EQ(129, postings->nextDoc());
    EXPECT_EQ(60, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}

TEST(BitPackPostingsRoundTripTest, ThreeDocsVIntOnly) {
    // Test with 3 docs (all VInt, no BitPack block)
    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // All freqs > 1: writeVInt(delta << 1), writeVInt(freq)
    docOut->writeVInt(0 << 1);  // doc 0
    docOut->writeVInt(10);
    docOut->writeVInt(1 << 1);  // doc 1
    docOut->writeVInt(20);
    docOut->writeVInt(1 << 1);  // doc 2
    docOut->writeVInt(30);

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

    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(10, postings->freq());

    EXPECT_EQ(1, postings->nextDoc());
    EXPECT_EQ(20, postings->freq());

    EXPECT_EQ(2, postings->nextDoc());
    EXPECT_EQ(30, postings->freq());

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}

TEST(BitPackPostingsRoundTripTest, WriterReaderRoundTrip) {
    // Full round-trip test using the actual writer and reader
    auto writeState = createWriteState();
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    Lucene104PostingsWriter writer(writeState);
    writer.setField(field);
    writer.startTerm();

    // Write 200 docs (1 full 128-doc block + 72 VInt tail)
    for (int i = 0; i < 200; i++) {
        int freq = (i % 7) + 1;
        writer.startDoc(i, freq);
    }

    TermState termState = writer.finishTerm();

    // Get the written bytes
    auto docBytes = writer.getBytes();

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docBytes));

    auto postings = reader.postings(field, termState);

    // Verify all 200 docs
    for (int i = 0; i < 200; i++) {
        EXPECT_EQ(i, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ((i % 7) + 1, postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());
}
