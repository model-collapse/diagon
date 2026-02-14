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
    // We'll manually encode the data to ensure correct format

    std::cout << "\n=== StreamVByte 4-Doc Debug Test ===" << std::endl;

    // Test data: doc IDs 0, 5, 10, 15 with frequencies 10, 20, 30, 40
    // Doc deltas: 0, 5, 5, 5
    uint32_t docDeltas[4] = {0, 5, 5, 5};
    uint32_t freqs[4] = {10, 20, 30, 40};

    std::cout << "Input doc deltas: [" << docDeltas[0] << ", " << docDeltas[1] << ", "
              << docDeltas[2] << ", " << docDeltas[3] << "]" << std::endl;
    std::cout << "Input frequencies: [" << freqs[0] << ", " << freqs[1] << ", " << freqs[2] << ", "
              << freqs[3] << "]" << std::endl;

    // Manually encode using StreamVByte
    uint8_t docDeltaEncoded[17];  // Max: 1 control + 4*4 data bytes
    int docDeltaBytes = StreamVByte::encode(docDeltas, 4, docDeltaEncoded);

    uint8_t freqEncoded[17];
    int freqBytes = StreamVByte::encode(freqs, 4, freqEncoded);

    std::cout << "\nEncoded doc deltas (" << docDeltaBytes << " bytes): ";
    for (int i = 0; i < docDeltaBytes; ++i) {
        printf("%02x ", docDeltaEncoded[i]);
    }
    std::cout << std::endl;

    std::cout << "Encoded frequencies (" << freqBytes << " bytes): ";
    for (int i = 0; i < freqBytes; ++i) {
        printf("%02x ", freqEncoded[i]);
    }
    std::cout << std::endl;

    // Manual decode to verify encoding
    uint32_t decodedDocDeltas[4];
    uint32_t decodedFreqs[4];
    StreamVByte::decode4(docDeltaEncoded, decodedDocDeltas);
    StreamVByte::decode4(freqEncoded, decodedFreqs);

    std::cout << "\nManual decode doc deltas: [" << decodedDocDeltas[0] << ", "
              << decodedDocDeltas[1] << ", " << decodedDocDeltas[2] << ", " << decodedDocDeltas[3]
              << "]" << std::endl;
    std::cout << "Manual decode frequencies: [" << decodedFreqs[0] << ", " << decodedFreqs[1]
              << ", " << decodedFreqs[2] << ", " << decodedFreqs[3] << "]" << std::endl;

    // Verify manual decode matches input
    ASSERT_EQ(docDeltas[0], decodedDocDeltas[0]);
    ASSERT_EQ(docDeltas[1], decodedDocDeltas[1]);
    ASSERT_EQ(docDeltas[2], decodedDocDeltas[2]);
    ASSERT_EQ(docDeltas[3], decodedDocDeltas[3]);
    ASSERT_EQ(freqs[0], decodedFreqs[0]);
    ASSERT_EQ(freqs[1], decodedFreqs[1]);
    ASSERT_EQ(freqs[2], decodedFreqs[2]);
    ASSERT_EQ(freqs[3], decodedFreqs[3]);
    std::cout << "✓ Manual decode verification passed" << std::endl;

    // Create buffer with encoded data
    ByteBuffersIndexOutput out("test.doc");
    out.writeBytes(docDeltaEncoded, docDeltaBytes);
    out.writeBytes(freqEncoded, freqBytes);

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
    termState.skipOffset = -1;

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
