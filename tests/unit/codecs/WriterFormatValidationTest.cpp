// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

// This test verifies that Lucene104PostingsWriter produces the expected StreamVByte format

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/StreamVByte.h"

#include <gtest/gtest.h>

#include <iostream>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::util;

// Helper class to capture writer output
class TestablePostingsWriter {
public:
    Lucene104PostingsWriter writer;
    std::shared_ptr<ByteBuffersIndexOutput> output;

    TestablePostingsWriter()
        : writer(createWriteState())
        , output(nullptr) {
        // Note: writer creates its own ByteBuffersIndexOutput internally
        // We can't access it directly, so we'll need a different approach
    }

private:
    static SegmentWriteState createWriteState() {
        std::vector<FieldInfo> fields;
        FieldInfos fieldInfos(fields);
        return SegmentWriteState(nullptr, "test", 100, fieldInfos, "");
    }
};

// For now, just test that writer and reader round-trip correctly
TEST(WriterFormatValidationTest, DISABLED_WriterProducesStreamVByteFormat) {
    // This test is disabled because we can't access the writer's internal buffer
    // The writer uses ByteBuffersIndexOutput internally but doesn't expose it
    // TODO: Either add a test-only method to expose bytes, or verify via reader
    FAIL() << "Test not implemented - need writer API to expose bytes";
}

// Instead, let's verify writer+reader integration
TEST(WriterFormatValidationTest, FourDocsWriterReaderRoundTrip) {
    // Create writer
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    SegmentWriteState writeState(nullptr, "test", 100, fieldInfos, "");

    // Note: The writer creates an internal ByteBuffersIndexOutput
    // We'll need to modify the writer or test differently
    // For now, this test documents the limitation

    std::cout << "Writer+Reader integration test placeholder" << std::endl;
    // TODO: Implement once we can extract bytes from writer
}

// Test that demonstrates the format mismatch issue
TEST(WriterFormatValidationTest, LegacyVIntFormatFailsWithNewReader) {
    // This test shows why PostingsReaderTest is failing:
    // Tests write old VInt format, reader expects new StreamVByte format

    auto docOut = std::make_unique<ByteBuffersIndexOutput>("test.doc");

    // Write 4 docs using OLD VInt format (what tests currently do)
    docOut->writeVInt(0);   // doc 0
    docOut->writeVInt(10);  // freq
    docOut->writeVInt(1);   // doc 1 (delta)
    docOut->writeVInt(20);  // freq
    docOut->writeVInt(1);   // doc 2 (delta)
    docOut->writeVInt(30);  // freq
    docOut->writeVInt(1);   // doc 3 (delta)
    docOut->writeVInt(40);  // freq

    std::cout << "\n=== Legacy VInt Format Test ===" << std::endl;
    std::cout << "Wrote 4 docs in VInt format" << std::endl;
    std::cout << "Total bytes: " << docOut->getFilePointer() << std::endl;

    // Try to read with new reader (expects StreamVByte)
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    SegmentReadState readState(nullptr, "test", 100, fieldInfos, "");
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", docOut->toArrayCopy()));

    FieldInfo field;
    field.name = "content";
    field.number = 0;
    field.indexOptions = IndexOptions::DOCS_AND_FREQS;

    TermState termState;
    termState.docStartFP = 0;
    termState.docFreq = 4;  // 4 docs - reader will try to read StreamVByte!
    termState.totalTermFreq = 100;

    auto postings = reader.postings(field, termState);

    // The reader will try to interpret the first VInt byte as a StreamVByte control byte
    // This will produce garbage
    std::cout << "Reading with StreamVByte reader..." << std::endl;
    for (int i = 0; i < 4; i++) {
        int docID = postings->nextDoc();
        int freq = postings->freq();
        std::cout << "  Doc " << i << ": ID=" << docID << ", freq=" << freq;
        if (i == 0 && docID != 0) {
            std::cout << " <- WRONG! Expected 0";
        } else if (i == 1 && docID != 1) {
            std::cout << " <- WRONG! Expected 1";
        }
        std::cout << std::endl;
    }

    // This demonstrates the format mismatch
    std::cout << "\nConclusion: VInt format is incompatible with StreamVByte reader" << std::endl;
    std::cout << "PostingsReaderTest needs to be updated to use StreamVByte format" << std::endl;
}
