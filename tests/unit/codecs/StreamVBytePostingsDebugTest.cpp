// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
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

// Helper functions from PostingsReaderTest
SegmentReadState createReadState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentReadState(nullptr, "test_segment", 100, fieldInfos, "");
}

FieldInfo createTestField(const std::string& name, IndexOptions options) {
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

TEST(BitPackPostingsDebugTest, VIntTailFourDocs) {
    // Test 4 docs using VInt tail (< 128, so no BitPack block)
    // Uses freq-in-docDelta encoding: freq=1 packed in low bit, non-1 freqs as VInt.

    std::cout << "\n=== BitPack 4-Doc VInt Tail Debug Test ===" << std::endl;

    // Test data: doc IDs 0, 5, 10, 15 with frequencies 10, 20, 30, 40
    // Doc deltas: 0, 5, 5, 5 — all freqs > 1

    // Write as VInt tail (docFreq < 128)
    ByteBuffersIndexOutput out("test.doc");

    // All freqs > 1: writeVInt(delta << 1), writeVInt(freq)
    out.writeVInt(0 << 1);   // doc 0 delta=0, low bit=0
    out.writeVInt(10);       // freq
    out.writeVInt(5 << 1);   // doc 1 delta=5, low bit=0
    out.writeVInt(20);       // freq
    out.writeVInt(5 << 1);   // doc 2 delta=5, low bit=0
    out.writeVInt(30);       // freq
    out.writeVInt(5 << 1);   // doc 3 delta=5, low bit=0
    out.writeVInt(40);       // freq

    std::cout << "Total bytes written: " << out.getFilePointer() << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 4;
    termState.totalTermFreq = 100;
    termState.skipStartFP = -1;

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);

    std::cout << "\n=== Testing reader ===" << std::endl;
    auto postings = reader.postings(field, termState);

    // Expected: docs 0, 5, 10, 15 with freqs 10, 20, 30, 40
    int docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(0, docID);
    EXPECT_EQ(10, postings->freq());

    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(5, docID);
    EXPECT_EQ(20, postings->freq());

    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(10, docID);
    EXPECT_EQ(30, postings->freq());

    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(15, docID);
    EXPECT_EQ(40, postings->freq());

    docID = postings->nextDoc();
    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, docID);

    std::cout << "\n[PASS] Test passed!" << std::endl;
}

TEST(BitPackPostingsDebugTest, BitPackBlockRoundTrip) {
    // Test exactly 128 docs (one full BitPack block, no VInt tail)

    std::cout << "\n=== BitPack128 Block Round-Trip Test ===" << std::endl;

    ByteBuffersIndexOutput out("test.doc");

    // Build 128 doc deltas with mixed freqs
    uint32_t modifiedDeltas[128];
    uint32_t freqs[128];

    for (int i = 0; i < 128; ++i) {
        uint32_t delta = (i == 0) ? 0 : 1;
        freqs[i] = static_cast<uint32_t>((i % 5) + 1);

        // Pack freq=1 into low bit
        if (freqs[i] == 1) {
            modifiedDeltas[i] = (delta << 1) | 1;
        } else {
            modifiedDeltas[i] = (delta << 1);
        }
    }

    // Encode as PFOR-Delta block
    uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
    // encode() may modify modifiedDeltas (PFOR masking), but OK since we saved freqs separately
    int encodedBytes = BitPacking::encode(modifiedDeltas, 128, encoded);
    out.writeBytes(encoded, encodedBytes);

    // Write non-1 freqs as VInts
    for (int i = 0; i < 128; ++i) {
        if (freqs[i] != 1) {
            out.writeVInt(static_cast<int32_t>(freqs[i]));
        }
    }

    std::cout << "BitPack block + VInt freqs: " << out.getFilePointer() << " bytes" << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 128;
    termState.totalTermFreq = 0;
    for (int i = 0; i < 128; ++i) termState.totalTermFreq += freqs[i];
    termState.skipStartFP = -1;

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    auto postings = reader.postings(field, termState);

    // Verify all 128 docs
    for (int i = 0; i < 128; i++) {
        int expectedDoc = i;
        int expectedFreq = (i % 5) + 1;
        EXPECT_EQ(expectedDoc, postings->nextDoc()) << "Doc " << i;
        EXPECT_EQ(expectedFreq, postings->freq()) << "Freq for doc " << i;
    }

    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, postings->nextDoc());

    std::cout << "\n[PASS] Test passed!" << std::endl;
}
