// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene105/Lucene105PostingsWriter.h"

#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"

#include <gtest/gtest.h>

using namespace diagon;
using namespace diagon::codecs;
using namespace diagon::codecs::lucene105;

class Lucene105PostingsWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create field infos (required by SegmentWriteState)
        std::vector<index::FieldInfo> infos;
        index::FieldInfo fi;
        fi.name = "body";
        fi.number = 0;
        fi.indexOptions = index::IndexOptions::DOCS_AND_FREQS;
        infos.push_back(fi);
        fieldInfos_ = std::make_unique<index::FieldInfos>(std::move(infos));

        // Create segment write state with required parameters
        writeState_ = std::make_unique<index::SegmentWriteState>(nullptr, "test_segment", 0,
                                                                 *fieldInfos_, "");

        // Create field info for "body" field
        fieldInfo_ = std::make_unique<index::FieldInfo>();
        fieldInfo_->name = "body";
        fieldInfo_->indexOptions = index::IndexOptions::DOCS_AND_FREQS;
    }

    std::unique_ptr<index::FieldInfos> fieldInfos_;
    std::unique_ptr<index::SegmentWriteState> writeState_;
    std::unique_ptr<index::FieldInfo> fieldInfo_;
};

TEST_F(Lucene105PostingsWriterTest, BasicWriteAndRead) {
    // Create writer
    Lucene105PostingsWriter writer(*writeState_);
    writer.setField(*fieldInfo_);

    // Write a simple term with 5 documents
    writer.startTerm();
    writer.startDoc(0, 1, 10);  // doc=0, freq=1, norm=10
    writer.startDoc(1, 2, 20);  // doc=1, freq=2, norm=20
    writer.startDoc(2, 3, 30);  // doc=2, freq=3, norm=30
    writer.startDoc(3, 1, 15);  // doc=3, freq=1, norm=15
    writer.startDoc(4, 4, 25);  // doc=4, freq=4, norm=25

    TermState state = writer.finishTerm();

    // Verify term state
    EXPECT_EQ(5, state.docFreq);
    EXPECT_EQ(11, state.totalTermFreq);  // 1+2+3+1+4
    EXPECT_EQ(0, state.docStartFP);
    EXPECT_EQ(-1, state.skipStartFP);  // No skip entries for small list
    EXPECT_EQ(0, state.skipEntryCount);

    writer.close();
}

TEST_F(Lucene105PostingsWriterTest, SkipEntriesCreated) {
    // Create writer
    Lucene105PostingsWriter writer(*writeState_);
    writer.setField(*fieldInfo_);

    // Write a term with 256 documents (should create 2 skip entries)
    writer.startTerm();
    for (int i = 0; i < 256; i++) {
        writer.startDoc(i, i % 10 + 1, static_cast<int8_t>(i % 127));
    }

    TermState state = writer.finishTerm();

    // Verify skip entries were created
    EXPECT_EQ(256, state.docFreq);
    EXPECT_GE(state.skipStartFP, 0);     // Skip data was written
    EXPECT_EQ(2, state.skipEntryCount);  // 2 skip entries (128 docs each)

    writer.close();
}

TEST_F(Lucene105PostingsWriterTest, ImpactsTrackedCorrectly) {
    // Create writer
    Lucene105PostingsWriter writer(*writeState_);
    writer.setField(*fieldInfo_);

    // Write a term with varying frequencies and norms
    writer.startTerm();

    // First block (128 docs): max_freq=50, max_norm=100
    for (int i = 0; i < 128; i++) {
        int freq = (i == 64) ? 50 : 10;      // Peak freq at doc 64
        int8_t norm = (i == 32) ? 100 : 50;  // Peak norm at doc 32
        writer.startDoc(i, freq, norm);
    }

    // Second block (128 docs): max_freq=75, max_norm=120
    for (int i = 128; i < 256; i++) {
        int freq = (i == 192) ? 75 : 15;      // Peak freq at doc 192
        int8_t norm = (i == 200) ? 120 : 60;  // Peak norm at doc 200
        writer.startDoc(i, freq, norm);
    }

    TermState state = writer.finishTerm();

    // Verify skip entries exist
    EXPECT_EQ(256, state.docFreq);
    EXPECT_EQ(2, state.skipEntryCount);

    // Read back skip data to verify impacts
    std::vector<uint8_t> skipBytes = writer.getSkipBytes();
    ASSERT_GT(skipBytes.size(), 0);

    // Parse skip data using ByteBuffersIndexInput
    auto skipIn = std::make_unique<store::ByteBuffersIndexInput>("skip_test", skipBytes);

    // Read num skip entries
    int32_t numSkipEntries = skipIn->readVInt();
    EXPECT_EQ(2, numSkipEntries);

    // Read first skip entry
    int32_t docDelta1 = skipIn->readVInt();
    int64_t docFPDelta1 = skipIn->readVLong();
    (void)docFPDelta1;
    int32_t maxFreq1 = skipIn->readVInt();
    uint8_t maxNorm1 = skipIn->readByte();

    EXPECT_GT(docDelta1, 0);
    EXPECT_EQ(50, maxFreq1);   // First block peak
    EXPECT_EQ(100, maxNorm1);  // First block peak

    // Read second skip entry
    int32_t docDelta2 = skipIn->readVInt();
    int64_t docFPDelta2 = skipIn->readVLong();
    (void)docFPDelta2;
    int32_t maxFreq2 = skipIn->readVInt();
    uint8_t maxNorm2 = skipIn->readByte();

    EXPECT_GT(docDelta2, 0);
    EXPECT_EQ(75, maxFreq2);   // Second block peak
    EXPECT_EQ(120, maxNorm2);  // Second block peak

    writer.close();
}

TEST_F(Lucene105PostingsWriterTest, NoSkipForSmallPostings) {
    // Create writer
    Lucene105PostingsWriter writer(*writeState_);
    writer.setField(*fieldInfo_);

    // Write a term with only 50 documents (< 128, no skip entries)
    writer.startTerm();
    for (int i = 0; i < 50; i++) {
        writer.startDoc(i, 5, 50);
    }

    TermState state = writer.finishTerm();

    // Verify no skip entries
    EXPECT_EQ(50, state.docFreq);
    EXPECT_EQ(-1, state.skipStartFP);  // No skip data
    EXPECT_EQ(0, state.skipEntryCount);

    // Verify no skip bytes written
    std::vector<uint8_t> skipBytes = writer.getSkipBytes();
    EXPECT_EQ(0, skipBytes.size());

    writer.close();
}

TEST_F(Lucene105PostingsWriterTest, MultipleTerms) {
    // Create writer
    Lucene105PostingsWriter writer(*writeState_);
    writer.setField(*fieldInfo_);

    // Write first term
    writer.startTerm();
    for (int i = 0; i < 200; i++) {
        writer.startDoc(i, 10, 50);
    }
    TermState state1 = writer.finishTerm();

    // Write second term
    writer.startTerm();
    for (int i = 0; i < 300; i++) {
        writer.startDoc(i, 15, 60);
    }
    TermState state2 = writer.finishTerm();

    // Verify both terms have independent skip data
    EXPECT_EQ(200, state1.docFreq);
    EXPECT_EQ(2, state1.skipEntryCount);  // 200 docs / 128 = 2 blocks

    EXPECT_EQ(300, state2.docFreq);
    EXPECT_EQ(3, state2.skipEntryCount);  // 300 docs / 128 = 3 blocks

    // Verify skip data is separate (different file pointers)
    EXPECT_NE(state1.skipStartFP, state2.skipStartFP);

    writer.close();
}

TEST_F(Lucene105PostingsWriterTest, StreamVByteIntegration) {
    // Verify that StreamVByte encoding still works with impacts tracking
    Lucene105PostingsWriter writer(*writeState_);
    writer.setField(*fieldInfo_);

    // Write documents in groups of 4 (StreamVByte buffer size)
    writer.startTerm();
    for (int i = 0; i < 16; i++) {  // 4 groups of 4
        writer.startDoc(i, i + 1, static_cast<int8_t>(i * 5));
    }

    TermState state = writer.finishTerm();
    (void)state;

    // Verify doc bytes were written (StreamVByte encoded)
    std::vector<uint8_t> docBytes = writer.getDocBytes();
    ASSERT_GT(docBytes.size(), 0);

    // Doc bytes should be compact (StreamVByte compression)
    // 16 docs with small freqs should use < 100 bytes
    EXPECT_LT(docBytes.size(), 100);

    writer.close();
}
