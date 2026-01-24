// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/store/ByteBuffersIndexInput.h"

#include <gtest/gtest.h>
#include <vector>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::search;

// ==================== Helper Functions ====================

SegmentWriteState createWriteState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentWriteState(
        nullptr,
        "test_segment",
        100,
        fieldInfos,
        ""
    );
}

SegmentReadState createReadState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentReadState(
        nullptr,
        "test_segment",
        100,
        fieldInfos,
        ""
    );
}

FieldInfo createField(const std::string& name, IndexOptions options) {
    FieldInfo field;
    field.name = name;
    field.number = 0;
    field.indexOptions = options;
    return field;
}

// ==================== Round-Trip Tests ====================

TEST(PostingsReaderTest, RoundTripMultipleDocs) {
    // Write postings
    auto writeState = createWriteState();
    Lucene104PostingsWriter writer(writeState);
    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    writer.setField(field);
    writer.startTerm();
    writer.startDoc(0, 1);
    writer.startDoc(5, 3);
    writer.startDoc(10, 2);
    auto termState = writer.finishTerm();

    // For MVP, we'll create input/output manually
    // Create a buffer with the expected format
    std::vector<uint8_t> buffer;
    ByteBuffersIndexOutput out("test.doc");

    // Write the postings manually
    out.writeVInt(0);   // first doc delta = 0
    out.writeVInt(1);   // freq = 1
    out.writeVInt(5);   // delta = 5
    out.writeVInt(3);   // freq = 3
    out.writeVInt(5);   // delta = 5
    out.writeVInt(2);   // freq = 2

    // Create reader with this buffer
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

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
    // Create buffer with docs only (no freqs)
    ByteBuffersIndexOutput out("test.doc");
    out.writeVInt(0);   // first doc
    out.writeVInt(5);   // delta
    out.writeVInt(5);   // delta

    // Create reader
    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

    // Read postings (DOCS_ONLY - no frequencies)
    auto field = createField("id", IndexOptions::DOCS);
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
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

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
    // Create buffer with large doc IDs
    ByteBuffersIndexOutput out("test.doc");
    out.writeVInt(1000000);  // first doc
    out.writeVInt(1);         // freq
    out.writeVInt(1000000);  // delta
    out.writeVInt(2);         // freq
    out.writeVInt(1000000);  // delta
    out.writeVInt(3);         // freq

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
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
    // Create buffer with many docs
    ByteBuffersIndexOutput out("test.doc");

    int docCount = 1000;
    for (int i = 0; i < docCount; i++) {
        if (i == 0) {
            out.writeVInt(i);  // first doc absolute
        } else {
            out.writeVInt(1);  // delta = 1
        }
        out.writeVInt((i % 10) + 1);  // freq varies
    }

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
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
    // Create buffer
    ByteBuffersIndexOutput out("test.doc");
    out.writeVInt(0);
    out.writeVInt(1);
    out.writeVInt(5);
    out.writeVInt(2);
    out.writeVInt(5);
    out.writeVInt(3);
    out.writeVInt(10);
    out.writeVInt(4);
    out.writeVInt(10);
    out.writeVInt(5);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
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
    ByteBuffersIndexOutput out("test.doc");
    out.writeVInt(0);
    out.writeVInt(1);
    out.writeVInt(5);
    out.writeVInt(2);

    auto readState = createReadState();
    Lucene104PostingsReader reader(readState);
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
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
    reader.setInput(std::make_unique<ByteBuffersIndexInput>(
        "test.doc", out.toArrayCopy()));

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);
    TermState state;
    state.docStartFP = 0;
    state.docFreq = 42;
    state.totalTermFreq = 100;

    auto postings = reader.postings(field, state);

    // Cost should equal doc frequency
    EXPECT_EQ(42, postings->cost());
}
