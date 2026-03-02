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

#include <cstring>
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
    // encode() may modify modDeltas (PFOR masking), but that's OK since we don't reuse them
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

TEST(BitPackPFORTest, ExceptionHandling) {
    // 128 values: 125 values fit in 3 bits, 3 outliers need 10 bits
    uint32_t values[128];
    uint32_t original[128];
    for (int i = 0; i < 125; i++) values[i] = i % 7;  // 0-6, fits in 3 bits
    values[125] = 500;   // needs 9 bits
    values[126] = 700;   // needs 10 bits
    values[127] = 1000;  // needs 10 bits
    std::memcpy(original, values, sizeof(values));

    uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
    int encBytes = BitPacking::encode(values, 128, encoded);

    // Verify token: should have exceptions and lower bit width
    uint8_t token = encoded[0];
    int bitsPerValue = token & 0x1F;
    int numExceptions = token >> 5;
    EXPECT_LT(bitsPerValue, 10);   // Should be < 10 (not worst case)
    EXPECT_GT(numExceptions, 0);   // Should have exceptions

    // Verify smaller than plain BitPacking would be
    // Plain BitPack128 with max=1000 needs 10 bits: 1 + ceil(128*10/8) = 161 bytes
    EXPECT_LT(encBytes, 161);

    // Verify round-trip
    uint32_t decoded[128];
    int decBytes = BitPacking::decode(encoded, 128, decoded);
    EXPECT_EQ(encBytes, decBytes);
    for (int i = 0; i < 128; i++) {
        EXPECT_EQ(original[i], decoded[i]) << "Mismatch at " << i;
    }
}

TEST(BitPackPFORTest, AllZeros) {
    uint32_t values[128] = {};
    uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
    int encBytes = BitPacking::encode(values, 128, encoded);

    // Token 0x00 + VInt(0) = 2 bytes
    EXPECT_EQ(2, encBytes);
    EXPECT_EQ(0, encoded[0]);  // token: bpv=0, numEx=0

    uint32_t decoded[128];
    int decBytes = BitPacking::decode(encoded, 128, decoded);
    EXPECT_EQ(encBytes, decBytes);
    for (int i = 0; i < 128; i++) {
        EXPECT_EQ(0u, decoded[i]);
    }
}

TEST(BitPackPFORTest, AllSameNonZero) {
    uint32_t values[128];
    for (int i = 0; i < 128; i++) values[i] = 42;

    uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
    int encBytes = BitPacking::encode(values, 128, encoded);

    // Token 0x00 + VInt(42) = 2 bytes (42 < 128, fits in 1 VInt byte)
    EXPECT_EQ(2, encBytes);
    EXPECT_EQ(0, encoded[0]);

    uint32_t decoded[128];
    int decBytes = BitPacking::decode(encoded, 128, decoded);
    EXPECT_EQ(encBytes, decBytes);
    for (int i = 0; i < 128; i++) {
        EXPECT_EQ(42u, decoded[i]);
    }
}

TEST(BitPackPFORTest, NoExceptions) {
    // All values fit in same bit width — no exceptions expected
    uint32_t values[128];
    for (int i = 0; i < 128; i++) values[i] = i;  // 0-127, needs 7 bits
    uint32_t original[128];
    std::memcpy(original, values, sizeof(values));

    uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
    BitPacking::encode(values, 128, encoded);

    uint8_t token = encoded[0];
    int numExceptions = token >> 5;
    EXPECT_EQ(0, numExceptions);  // No exceptions for uniform data

    uint32_t decoded[128];
    BitPacking::decode(encoded, 128, decoded);
    for (int i = 0; i < 128; i++) {
        EXPECT_EQ(original[i], decoded[i]) << "Mismatch at " << i;
    }
}

TEST(BitPackPFORTest, PostingsWriterReaderRoundTripWithExceptions) {
    // Full round-trip through PostingsWriter → PostingsReader with data that triggers exceptions
    auto writeState = createWriteState();
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    Lucene104PostingsWriter writer(writeState);
    writer.setField(field);
    writer.startTerm();

    // Write 200 docs with varying gaps to create exception-triggering deltas
    // Most docs have small deltas (1-3), a few have large deltas (1000+)
    int lastDoc = -1;
    for (int i = 0; i < 200; i++) {
        int docId;
        if (i % 50 == 49) {
            // Every 50th doc has a large gap
            docId = lastDoc + 5000;
        } else {
            docId = lastDoc + (i % 3) + 1;
        }
        lastDoc = docId;
        int freq = (i % 7) + 1;
        writer.startDoc(docId, freq);
    }

    TermState termState = writer.finishTerm();
    auto docBytes = writer.getBytes();

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docBytes));

    auto postings = reader.postings(field, termState);

    // Verify all 200 docs match
    lastDoc = -1;
    for (int i = 0; i < 200; i++) {
        int expectedDoc;
        if (i % 50 == 49) {
            expectedDoc = lastDoc + 5000;
        } else {
            expectedDoc = lastDoc + (i % 3) + 1;
        }
        lastDoc = expectedDoc;

        EXPECT_EQ(expectedDoc, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ((i % 7) + 1, postings->freq()) << "Freq for doc " << i;
    }

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
