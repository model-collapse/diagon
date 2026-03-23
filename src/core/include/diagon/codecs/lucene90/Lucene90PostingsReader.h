// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsReaderBase.h"
#include "diagon/codecs/lucene90/Lucene90ForUtil.h"
#include "diagon/codecs/lucene90/Lucene90PForUtil.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Extended term state for Lucene90 postings format.
 *
 * Includes fields not present in lucene104::TermState:
 * - singletonDocID: pulsed posting optimization for docFreq==1
 * - lastPosBlockOffset: offset to the VInt position tail
 * - payStartFP: .pay file pointer (not used in MVP)
 */
struct Lucene90TermState {
    int64_t docStartFP = 0;
    int64_t posStartFP = -1;
    int64_t payStartFP = -1;
    int docFreq = 0;
    int64_t totalTermFreq = 0;
    int64_t skipOffset = -1;
    int64_t lastPosBlockOffset = -1;
    int singletonDocID = -1;
};

/**
 * Reads Lucene90-format postings from .doc and .pos files.
 *
 * Decode-only reader for OpenSearch 2.11 / Lucene 9.x indices.
 * Uses PForUtil (128-block PFOR with exceptions) for block decoding,
 * with separate doc-delta and frequency PFOR blocks.
 *
 * Key differences from Lucene104PostingsReader:
 * - int64_t[128] PForUtil decode (not uint32_t[128] BitPack)
 * - Doc deltas use decodeAndPrefixSum() -> absolute doc IDs
 * - Frequencies in a separate PFOR block (not packed in low bit of doc delta)
 * - No separate .skp file - skip data inline in .doc (deferred to later phase)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsReader
 */
class Lucene90PostingsReader : public PostingsReaderBase {
public:
    explicit Lucene90PostingsReader(index::SegmentReadState& state);
    ~Lucene90PostingsReader();

    // PostingsReaderBase interface (accepts lucene104::TermState)
    std::unique_ptr<index::PostingsEnum> postings(
        const index::FieldInfo& fieldInfo, const lucene104::TermState& termState,
        bool useBatch) override;

    std::unique_ptr<index::PostingsEnum> postingsWithPositions(
        const index::FieldInfo& fieldInfo, const lucene104::TermState& termState) override;

    std::unique_ptr<index::PostingsEnum> impactsPostings(
        const index::FieldInfo& fieldInfo, const lucene104::TermState& termState) override;

    void close() override;

    // Lucene90-specific overloads (used by BlockTreeTermsReader in Phase C.4)
    std::unique_ptr<index::PostingsEnum> postings(
        const index::FieldInfo& fieldInfo, const Lucene90TermState& termState);

    std::unique_ptr<index::PostingsEnum> postingsWithPositions(
        const index::FieldInfo& fieldInfo, const Lucene90TermState& termState);

    /**
     * Read postings sub-header from .tmd and open .doc/.pos files.
     * Called by Lucene90BlockTreeTermsReader during construction.
     *
     * Based on: org.apache.lucene.backward_codecs.lucene90.Lucene90PostingsReader.init()
     */
    void init(store::IndexInput& metaIn, index::SegmentReadState& state);

    // Test setters
    void setDocInput(std::unique_ptr<store::IndexInput> input) { docIn_ = std::move(input); }
    void setPosInput(std::unique_ptr<store::IndexInput> input) { posIn_ = std::move(input); }

private:
    std::unique_ptr<store::IndexInput> docIn_;
    std::unique_ptr<store::IndexInput> posIn_;
    std::string segmentName_;
    std::string segmentSuffix_;
};

/**
 * PostingsEnum for Lucene90 doc/freq iteration with 128-block PForUtil decode.
 *
 * Block format (.doc file):
 * - PFOR blocks of 128: [doc-delta PFOR block] [freq PFOR block]
 * - VInt tail (< 128 docs): code = (docDelta << 1) | (freq==1 ? 1 : 0)
 * - Singleton (docFreq==1): stored in TermState, no data in file
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsReader.BlockDocsEnum
 */
class Lucene90BlockDocsEnum : public index::PostingsEnum {
public:
    Lucene90BlockDocsEnum(std::unique_ptr<store::IndexInput> docIn,
                          const Lucene90TermState& termState, bool indexHasFreq);

    int docID() const override { return doc_; }
    int nextDoc() override;
    int advance(int target) override;
    int64_t cost() const override { return docFreq_; }
    int freq() const override { return freq_; }

private:
    static constexpr int BLOCK_SIZE = ForUtil::BLOCK_SIZE;  // 128

    std::unique_ptr<store::IndexInput> docIn_;
    ForUtil forUtil_;
    PForUtil pforUtil_;

    int docFreq_;
    int64_t totalTermFreq_;
    bool indexHasFreq_;
    int singletonDocID_;

    int doc_;
    int freq_;

    // Block state
    int64_t docBuffer_[BLOCK_SIZE + 1];  // +1 for NO_MORE_DOCS sentinel
    int64_t freqBuffer_[BLOCK_SIZE];
    int docBufferUpto_;
    int blockUpto_;
    int64_t accum_;

    void refillDocs();

    static void readVIntBlock(store::IndexInput& in, int64_t* docBuffer,
                              int64_t* freqBuffer, int num, bool indexHasFreq);

    static void prefixSum(int64_t* buffer, int count, int64_t base);
};

/**
 * PostingsEnum for Lucene90 doc/freq/position iteration.
 *
 * Extends doc enum with position support from .pos file.
 * Position blocks also use PForUtil (128-block), decoded as raw deltas
 * (not prefix-summed -- delta accumulation happens in nextPosition()).
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsReader.BlockPostingsEnum
 */
class Lucene90BlockPosEnum : public index::PostingsEnum {
public:
    Lucene90BlockPosEnum(std::unique_ptr<store::IndexInput> docIn,
                         std::unique_ptr<store::IndexInput> posIn,
                         const Lucene90TermState& termState, bool indexHasFreq);

    int docID() const override { return doc_; }
    int nextDoc() override;
    int advance(int target) override;
    int64_t cost() const override { return docFreq_; }
    int freq() const override { return freq_; }
    int nextPosition() override;

private:
    static constexpr int BLOCK_SIZE = ForUtil::BLOCK_SIZE;

    std::unique_ptr<store::IndexInput> docIn_;
    std::unique_ptr<store::IndexInput> posIn_;
    ForUtil forUtil_;
    PForUtil pforUtil_;

    int docFreq_;
    int64_t totalTermFreq_;
    bool indexHasFreq_;
    int singletonDocID_;

    int doc_;
    int freq_;
    int position_;

    // Doc block state
    int64_t docBuffer_[BLOCK_SIZE + 1];
    int64_t freqBuffer_[BLOCK_SIZE];
    int docBufferUpto_;
    int blockUpto_;
    int64_t accum_;

    // Position block state
    int64_t posDeltaBuffer_[BLOCK_SIZE];
    int posBufUpto_;
    int posPendingCount_;
    int64_t lastPosBlockFP_;
    int64_t totalPosRead_;

    void refillDocs();
    void refillPositions();
    void skipPositions();

    static void readVIntBlock(store::IndexInput& in, int64_t* docBuffer,
                              int64_t* freqBuffer, int num, bool indexHasFreq);
    static void prefixSum(int64_t* buffer, int count, int64_t base);
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
