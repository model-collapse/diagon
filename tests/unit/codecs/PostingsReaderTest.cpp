// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/StreamVByte.h"

#include <gtest/gtest.h>

#include <vector>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::search;
using namespace diagon::util;

// ==================== Helper Functions ====================

SegmentWriteState createWriteState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentWriteState(nullptr, "test_segment", 100, fieldInfos, "");
}

SegmentReadState createReadState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentReadState(nullptr, "test_segment", 100, fieldInfos, "");
}

FieldInfo createField(const std::string& name, IndexOptions options) {
    FieldInfo field;
    field.name = name;
    field.number = 0;
    field.indexOptions = options;
    return field;
}

// Helper: Write posting list in StreamVByte format (Phase 2a)
// Uses StreamVByte for groups of 4, VInt for remainder
void writePostingsStreamVByte(ByteBuffersIndexOutput& out, const std::vector<int>& docDeltas,
                               const std::vector<int>& freqs, bool writeFreqs) {
    size_t numDocs = docDeltas.size();
    size_t pos = 0;

    // Write full groups of 4 using StreamVByte
    while (pos + 4 <= numDocs) {
        uint32_t docGroup[4];
        uint32_t freqGroup[4];

        for (int i = 0; i < 4; ++i) {
            docGroup[i] = static_cast<uint32_t>(docDeltas[pos + i]);
            freqGroup[i] = static_cast<uint32_t>(freqs[pos + i]);
        }

        // Encode and write doc deltas
        uint8_t docEncoded[17];
        int docBytes = StreamVByte::encode(docGroup, 4, docEncoded);
        out.writeBytes(docEncoded, docBytes);

        // Encode and write frequencies
        if (writeFreqs) {
            uint8_t freqEncoded[17];
            int freqBytes = StreamVByte::encode(freqGroup, 4, freqEncoded);
            out.writeBytes(freqEncoded, freqBytes);
        }

        pos += 4;
    }

    // Write remaining docs (< 4) using VInt
    while (pos < numDocs) {
        out.writeVInt(docDeltas[pos]);
        if (writeFreqs) {
            out.writeVInt(freqs[pos]);
        }
        pos++;
    }
}

// ==================== Round-Trip Tests ====================

TEST(PostingsReaderTest, RoundTripMultipleDocs) {
    // Create buffer with 3 docs (< 4, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Doc deltas: 0, 5, 5 (for docs 0, 5, 10)
    // Freqs: 1, 3, 2
    std::vector<int> docDeltas = {0, 5, 5};
    std::vector<int> freqs = {1, 3, 2};
    writePostingsStreamVByte(out, docDeltas, freqs, true);

    // Create reader with this buffer
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    // Read postings
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 3;
    state.totalTermFreq = 6;

    auto postings = reader.postings(field, state);

    // Verify first doc
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(0, postings->docID());
    EXPECT_EQ(1, postings->freq());

    // Verify second doc
    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(5, postings->docID());
    EXPECT_EQ(3, postings->freq());

    // Verify third doc
    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(10, postings->docID());
    EXPECT_EQ(2, postings->freq());

    // No more docs
    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->nextDoc());
}

TEST(PostingsReaderTest, DocsOnlyMode) {
    // Create buffer with docs only (no freqs) - 3 docs (< 4, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("id", IndexOptions::DOCS);

    // Doc deltas: 0, 5, 5 (for docs 0, 5, 10)
    // No freqs for DOCS_ONLY mode
    std::vector<int> docDeltas = {0, 5, 5};
    std::vector<int> freqs = {1, 1, 1};  // Dummy values, won't be written
    writePostingsStreamVByte(out, docDeltas, freqs, false);  // writeFreqs=false

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    // Read postings (DOCS_ONLY - no frequencies)
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 3;
    state.totalTermFreq = -1;  // Not tracked for DOCS_ONLY

    auto postings = reader.postings(field, state);

    // Verify docs (freq should default to 1)
    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->nextDoc());
}

TEST(PostingsReaderTest, EmptyPostings) {
    // Create empty buffer
    ByteBuffersIndexOutput out("test.doc");

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 0;  // No docs
    state.totalTermFreq = 0;

    auto postings = reader.postings(field, state);

    // Should immediately return NO_MORE_DOCS
    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->nextDoc());
}

TEST(PostingsReaderTest, LargeDocIDs) {
    // Create buffer with large doc IDs - 3 docs (< 4, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Doc deltas: 1000000, 1000000, 1000000 (for docs 1000000, 2000000, 3000000)
    // Freqs: 1, 2, 3
    std::vector<int> docDeltas = {1000000, 1000000, 1000000};
    std::vector<int> freqs = {1, 2, 3};
    writePostingsStreamVByte(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = 3;
    state.totalTermFreq = 6;

    auto postings = reader.postings(field, state);

    EXPECT_EQ(1000000, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(2000000, postings->nextDoc());
    EXPECT_EQ(2, postings->freq());

    EXPECT_EQ(3000000, postings->nextDoc());
    EXPECT_EQ(3, postings->freq());
}

TEST(PostingsReaderTest, ManyDocs) {
    // Create buffer with many docs - 1000 docs (250 StreamVByte groups of 4)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    int docCount = 1000;
    std::vector<int> docDeltas;
    std::vector<int> freqs;

    for (int i = 0; i < docCount; i++) {
        if (i == 0) {
            docDeltas.push_back(0);  // first doc absolute
        } else {
            docDeltas.push_back(1);  // delta = 1
        }
        freqs.push_back((i % 10) + 1);  // freq varies
    }

    writePostingsStreamVByte(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = docCount;
    state.totalTermFreq = 5500;  // sum of freqs

    auto postings = reader.postings(field, state);

    // Verify all docs
    for (int i = 0; i < docCount; i++) {
        EXPECT_EQ(i, postings->nextDoc());
        EXPECT_EQ(i, postings->docID());
        EXPECT_EQ((i % 10) + 1, postings->freq());
    }

    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->nextDoc());
}

// ==================== Advance Tests ====================

TEST(PostingsReaderTest, AdvanceBasic) {
    // Create buffer with 5 docs (1 StreamVByte group of 4 + 1 VInt)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Docs: 0, 5, 10, 20, 30 with freqs: 1, 2, 3, 4, 5
    // Doc deltas: 0, 5, 5, 10, 10
    std::vector<int> docDeltas = {0, 5, 5, 10, 10};
    std::vector<int> freqs = {1, 2, 3, 4, 5};
    writePostingsStreamVByte(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = 5;
    state.totalTermFreq = 15;

    auto postings = reader.postings(field, state);

    // Advance to doc >= 10
    EXPECT_EQ(10, postings->advance(10));
    EXPECT_EQ(10, postings->docID());
    EXPECT_EQ(3, postings->freq());

    // Continue iteration (should get doc 20 with freq 4)
    EXPECT_EQ(20, postings->nextDoc());
    EXPECT_EQ(4, postings->freq());

    // Next doc should be 30 with freq 5
    EXPECT_EQ(30, postings->nextDoc());
    EXPECT_EQ(5, postings->freq());
}

TEST(PostingsReaderTest, AdvancePastEnd) {
    // 2 docs (< 4, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Docs: 0, 5 with freqs: 1, 2
    // Doc deltas: 0, 5
    std::vector<int> docDeltas = {0, 5};
    std::vector<int> freqs = {1, 2};
    writePostingsStreamVByte(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = 2;
    state.totalTermFreq = 3;

    auto postings = reader.postings(field, state);

    // Advance past all docs
    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->advance(1000));
}

// ==================== Cost Tests ====================

TEST(PostingsReaderTest, Cost) {
    ByteBuffersIndexOutput out("test.doc");

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 42;
    state.totalTermFreq = 100;

    auto postings = reader.postings(field, state);

    // Cost should equal doc frequency
    EXPECT_EQ(42, postings->cost());
}
