// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/BitPacking.h"

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

// Helper: Write posting list using BitPack128 format with freq-in-docDelta encoding.
// For freq==1: docDelta = (docDelta << 1) | 1 (no separate freq needed)
// For freq>1:  docDelta = (docDelta << 1) | 0 (freq written as VInt after block)
// Uses BitPack128 for groups of 128, VInt for remainder.
void writePostingsBitPack(ByteBuffersIndexOutput& out, const std::vector<int>& docDeltas,
                          const std::vector<int>& freqs, bool writeFreqs) {
    size_t numDocs = docDeltas.size();
    size_t pos = 0;

    // Write full blocks of 128 using BitPacking
    while (pos + 128 <= numDocs) {
        uint32_t docGroup[128];

        for (int i = 0; i < 128; ++i) {
            if (writeFreqs) {
                if (freqs[pos + i] == 1) {
                    docGroup[i] = (static_cast<uint32_t>(docDeltas[pos + i]) << 1) | 1;
                } else {
                    docGroup[i] = static_cast<uint32_t>(docDeltas[pos + i]) << 1;
                }
            } else {
                docGroup[i] = static_cast<uint32_t>(docDeltas[pos + i]);
            }
        }

        // Encode and write BitPack block
        uint8_t encoded[BitPacking::maxBytesPerBlock(128)];
        int encodedBytes = BitPacking::encode(docGroup, 128, encoded);
        out.writeBytes(encoded, encodedBytes);

        // Write only non-1 frequencies as VInts after the block
        if (writeFreqs) {
            for (int i = 0; i < 128; ++i) {
                if (freqs[pos + i] != 1) {
                    out.writeVInt(freqs[pos + i]);
                }
            }
        }

        pos += 128;
    }

    // Write remaining docs (< 128) using VInt with freq-in-docDelta packing
    while (pos < numDocs) {
        if (writeFreqs) {
            if (freqs[pos] == 1) {
                out.writeVInt((docDeltas[pos] << 1) | 1);
            } else {
                out.writeVInt(docDeltas[pos] << 1);
                out.writeVInt(freqs[pos]);
            }
        } else {
            out.writeVInt(docDeltas[pos]);
        }
        pos++;
    }
}

// ==================== Round-Trip Tests ====================

TEST(PostingsReaderTest, RoundTripMultipleDocs) {
    // Create buffer with 3 docs (< 128, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Doc deltas: 0, 5, 5 (for docs 0, 5, 10)
    // Freqs: 1, 3, 2
    std::vector<int> docDeltas = {0, 5, 5};
    std::vector<int> freqs = {1, 3, 2};
    writePostingsBitPack(out, docDeltas, freqs, true);

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
    // Create buffer with docs only (no freqs) - 3 docs (< 128, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("id", IndexOptions::DOCS);

    // Doc deltas: 0, 5, 5 (for docs 0, 5, 10)
    std::vector<int> docDeltas = {0, 5, 5};
    std::vector<int> freqs = {1, 1, 1};
    writePostingsBitPack(out, docDeltas, freqs, false);

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    // Read postings (DOCS_ONLY)
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 3;
    state.totalTermFreq = -1;

    auto postings = reader.postings(field, state);

    EXPECT_EQ(0, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(5, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(10, postings->nextDoc());
    EXPECT_EQ(1, postings->freq());

    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->nextDoc());
}

TEST(PostingsReaderTest, EmptyPostings) {
    ByteBuffersIndexOutput out("test.doc");

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 0;
    state.totalTermFreq = 0;

    auto postings = reader.postings(field, state);

    EXPECT_EQ(DocIdSetIterator::NO_MORE_DOCS, postings->nextDoc());
}

TEST(PostingsReaderTest, LargeDocIDs) {
    // 3 docs with large deltas (< 128, so VInt fallback)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    std::vector<int> docDeltas = {1000000, 1000000, 1000000};
    std::vector<int> freqs = {1, 2, 3};
    writePostingsBitPack(out, docDeltas, freqs, true);

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
    // 1000 docs (7 full BitPack128 blocks + VInt tail of 104)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    int docCount = 1000;
    std::vector<int> docDeltas;
    std::vector<int> freqs;

    for (int i = 0; i < docCount; i++) {
        if (i == 0) {
            docDeltas.push_back(0);
        } else {
            docDeltas.push_back(1);
        }
        freqs.push_back((i % 10) + 1);
    }

    writePostingsBitPack(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = docCount;
    state.totalTermFreq = 5500;

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
    // 5 docs (< 128, so VInt tail)
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    std::vector<int> docDeltas = {0, 5, 5, 10, 10};
    std::vector<int> freqs = {1, 2, 3, 4, 5};
    writePostingsBitPack(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = 5;
    state.totalTermFreq = 15;

    auto postings = reader.postings(field, state);

    EXPECT_EQ(10, postings->advance(10));
    EXPECT_EQ(10, postings->docID());
    EXPECT_EQ(3, postings->freq());

    EXPECT_EQ(20, postings->nextDoc());
    EXPECT_EQ(4, postings->freq());

    EXPECT_EQ(30, postings->nextDoc());
    EXPECT_EQ(5, postings->freq());
}

TEST(PostingsReaderTest, AdvancePastEnd) {
    ByteBuffersIndexOutput out("test.doc");
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    std::vector<int> docDeltas = {0, 5};
    std::vector<int> freqs = {1, 2};
    writePostingsBitPack(out, docDeltas, freqs, true);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>("test.doc", out.toArrayCopy()));

    TermState state;
    state.docStartFP = 0;
    state.docFreq = 2;
    state.totalTermFreq = 3;

    auto postings = reader.postings(field, state);

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

    EXPECT_EQ(42, postings->cost());
}
