// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <gtest/gtest.h>

#include <memory>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;

// ==================== Helper Functions ====================

SegmentWriteState createTestState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentWriteState(nullptr,  // directory (not needed for ByteBuffersIndexOutput)
                             "test_segment",
                             100,  // maxDoc
                             fieldInfos,
                             ""  // suffix
    );
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

// ==================== Basic Tests ====================

TEST(PostingsWriterTest, Construction) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    // Writer should be constructed successfully
    EXPECT_EQ(0, writer.getFilePointer());
}

TEST(PostingsWriterTest, SingleTermSingleDoc) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    // Set field (DOCS_AND_FREQS)
    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    // Write single term with single doc
    writer.startTerm();
    writer.startDoc(5, 3);  // docID=5, freq=3

    auto termState = writer.finishTerm();

    // Verify term state
    EXPECT_EQ(0, termState.docStartFP);
    EXPECT_EQ(1, termState.docFreq);
    EXPECT_EQ(3, termState.totalTermFreq);
    EXPECT_EQ(-1, termState.skipOffset);  // No skip list
}

TEST(PostingsWriterTest, SingleTermMultipleDocs) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    // Set field
    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    // Write term with multiple docs
    writer.startTerm();
    writer.startDoc(0, 1);
    writer.startDoc(5, 3);
    writer.startDoc(10, 2);

    auto termState = writer.finishTerm();

    // Verify term state
    EXPECT_EQ(0, termState.docStartFP);
    EXPECT_EQ(3, termState.docFreq);
    EXPECT_EQ(6, termState.totalTermFreq);  // 1+3+2
}

TEST(PostingsWriterTest, MultipleTerms) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    // Set field
    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    // Term 1
    writer.startTerm();
    writer.startDoc(0, 1);
    writer.startDoc(5, 2);
    auto term1State = writer.finishTerm();

    int64_t term1EndFP = writer.getFilePointer();

    // Term 2
    writer.startTerm();
    writer.startDoc(2, 3);
    writer.startDoc(7, 1);
    auto term2State = writer.finishTerm();

    // Verify term states have different file pointers
    EXPECT_LT(term1State.docStartFP, term2State.docStartFP);
    EXPECT_EQ(term1EndFP, term2State.docStartFP);

    // Verify term frequencies
    EXPECT_EQ(2, term1State.docFreq);
    EXPECT_EQ(3, term1State.totalTermFreq);
    EXPECT_EQ(2, term2State.docFreq);
    EXPECT_EQ(4, term2State.totalTermFreq);
}

TEST(PostingsWriterTest, DocsOnlyMode) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    // Set field to DOCS_ONLY (no frequencies)
    auto field = createTestField("id", IndexOptions::DOCS);
    writer.setField(field);

    // Write term
    writer.startTerm();
    writer.startDoc(5, 1);  // freq ignored for DOCS_ONLY
    writer.startDoc(10, 1);

    auto termState = writer.finishTerm();

    // Verify: totalTermFreq should be -1 for DOCS_ONLY
    EXPECT_EQ(2, termState.docFreq);
    EXPECT_EQ(-1, termState.totalTermFreq);
}

// ==================== Error Tests ====================

TEST(PostingsWriterTest, DocOutOfOrder) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    writer.startTerm();
    writer.startDoc(5, 1);

    // Try to add doc with lower ID
    EXPECT_THROW(writer.startDoc(3, 1), std::invalid_argument);
}

TEST(PostingsWriterTest, DuplicateDocID) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    writer.startTerm();
    writer.startDoc(5, 1);

    // Try to add same doc ID again
    EXPECT_THROW(writer.startDoc(5, 1), std::invalid_argument);
}

TEST(PostingsWriterTest, NegativeDocID) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    writer.startTerm();

    // Negative doc ID should fail
    EXPECT_THROW(writer.startDoc(-1, 1), std::invalid_argument);
}

TEST(PostingsWriterTest, ZeroFreq) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    writer.startTerm();

    // Zero frequency should fail
    EXPECT_THROW(writer.startDoc(5, 0), std::invalid_argument);
}

TEST(PostingsWriterTest, NegativeFreq) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    writer.startTerm();

    // Negative frequency should fail
    EXPECT_THROW(writer.startDoc(5, -1), std::invalid_argument);
}

// ==================== Data Size Tests ====================

TEST(PostingsWriterTest, FilePointerProgression) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    EXPECT_EQ(0, writer.getFilePointer());

    writer.startTerm();
    int64_t startFP = writer.getFilePointer();

    // Phase 2a: With StreamVByte buffering, file pointer doesn't advance
    // until buffer flushes (4 docs) or finishTerm() is called
    writer.startDoc(0, 1);
    int64_t afterDoc1 = writer.getFilePointer();
    EXPECT_EQ(afterDoc1, startFP);  // No write yet (buffer not full)

    writer.startDoc(5, 3);
    int64_t afterDoc2 = writer.getFilePointer();
    EXPECT_EQ(afterDoc2, startFP);  // Still no write (buffer not full)

    // After finishTerm, remaining buffered docs are written
    writer.finishTerm();
    int64_t afterFinish = writer.getFilePointer();
    EXPECT_GT(afterFinish, startFP);  // Now data is written
}

TEST(PostingsWriterTest, LargeDocIDs) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    // Write term with large doc IDs
    writer.startTerm();
    writer.startDoc(1000000, 1);
    writer.startDoc(2000000, 2);
    writer.startDoc(3000000, 3);

    auto termState = writer.finishTerm();

    EXPECT_EQ(3, termState.docFreq);
    EXPECT_EQ(6, termState.totalTermFreq);
}

TEST(PostingsWriterTest, ManyDocs) {
    auto state = createTestState();
    Lucene104PostingsWriter writer(state);

    auto field = createTestField("content", IndexOptions::DOCS_AND_FREQS);
    writer.setField(field);

    // Write term with many docs
    writer.startTerm();
    int64_t totalFreq = 0;
    for (int i = 0; i < 1000; i++) {
        int freq = (i % 10) + 1;
        writer.startDoc(i, freq);
        totalFreq += freq;
    }

    auto termState = writer.finishTerm();

    EXPECT_EQ(1000, termState.docFreq);
    EXPECT_EQ(totalFreq, termState.totalTermFreq);
}
