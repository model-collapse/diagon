// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
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

TEST(StreamVBytePostingsDebugTest, FourDocsRoundTrip) {
    // Test exactly 4 docs (one StreamVByte group, no VInt fallback)
    // Uses freq-in-docDelta encoding: freq=1 packed in low bit, non-1 freqs as VInt tail.

    std::cout << "\n=== StreamVByte 4-Doc Debug Test (freq-in-docDelta) ===" << std::endl;

    // Test data: doc IDs 0, 5, 10, 15 with frequencies 10, 20, 30, 40
    // Doc deltas: 0, 5, 5, 5
    // All freqs > 1, so modified deltas = (delta << 1) | 0
    uint32_t docDeltas[4] = {0, 5, 5, 5};
    uint32_t freqs[4] = {10, 20, 30, 40};

    // Pack freq into low bit of doc delta
    uint32_t modifiedDeltas[4];
    for (int i = 0; i < 4; ++i) {
        // All freqs > 1: low bit = 0
        modifiedDeltas[i] = docDeltas[i] << 1;
    }

    std::cout << "Original doc deltas: [" << docDeltas[0] << ", " << docDeltas[1] << ", "
              << docDeltas[2] << ", " << docDeltas[3] << "]" << std::endl;
    std::cout << "Modified deltas (freq packed): [" << modifiedDeltas[0] << ", " << modifiedDeltas[1]
              << ", " << modifiedDeltas[2] << ", " << modifiedDeltas[3] << "]" << std::endl;
    std::cout << "Frequencies: [" << freqs[0] << ", " << freqs[1] << ", " << freqs[2] << ", "
              << freqs[3] << "]" << std::endl;

    // StreamVByte encode modified doc deltas
    uint8_t docDeltaEncoded[17];
    int docDeltaBytes = StreamVByte::encode(modifiedDeltas, 4, docDeltaEncoded);

    std::cout << "\nEncoded modified deltas (" << docDeltaBytes << " bytes): ";
    for (int i = 0; i < docDeltaBytes; ++i) {
        printf("%02x ", docDeltaEncoded[i]);
    }
    std::cout << std::endl;

    // Manual decode to verify encoding
    uint32_t decodedModifiedDeltas[4];
    StreamVByte::decode4(docDeltaEncoded, decodedModifiedDeltas);

    std::cout << "\nManual decode modified deltas: [" << decodedModifiedDeltas[0] << ", "
              << decodedModifiedDeltas[1] << ", " << decodedModifiedDeltas[2] << ", "
              << decodedModifiedDeltas[3] << "]" << std::endl;

    // Verify manual decode matches modified deltas
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(modifiedDeltas[i], decodedModifiedDeltas[i]);
    }
    std::cout << "✓ Manual decode verification passed" << std::endl;

    // Create buffer: StreamVByte modified deltas + VInt freqs for non-1 entries
    ByteBuffersIndexOutput out("test.doc");
    out.writeBytes(docDeltaEncoded, docDeltaBytes);
    // All freqs > 1, so write all as VInts
    for (int i = 0; i < 4; ++i) {
        out.writeVInt(static_cast<int32_t>(freqs[i]));
    }

    std::cout << "\nTotal bytes written: " << out.getFilePointer() << std::endl;

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    // Setup term state
    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 4;          // 4 documents
    termState.totalTermFreq = 100;  // 10+20+30+40
    termState.skipStartFP = -1;

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);

    std::cout << "\n=== Testing reader ===" << std::endl;
    auto postings = reader.postings(field, termState);

    // Expected: docs 0, 5, 10, 15 with freqs 10, 20, 30, 40
    std::cout << "Reading doc 0..." << std::endl;
    int docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(0, docID);
    EXPECT_EQ(10, postings->freq());

    std::cout << "Reading doc 1..." << std::endl;
    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(5, docID);
    EXPECT_EQ(20, postings->freq());

    std::cout << "Reading doc 2..." << std::endl;
    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(10, docID);
    EXPECT_EQ(30, postings->freq());

    std::cout << "Reading doc 3..." << std::endl;
    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << ", freq=" << postings->freq() << std::endl;
    EXPECT_EQ(15, docID);
    EXPECT_EQ(40, postings->freq());

    std::cout << "Checking for NO_MORE_DOCS..." << std::endl;
    docID = postings->nextDoc();
    std::cout << "  Got docID=" << docID << std::endl;
    EXPECT_EQ(PostingsEnum::NO_MORE_DOCS, docID);

    std::cout << "\n✓ Test passed!" << std::endl;
}
