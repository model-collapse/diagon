// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsReaderBase.h"
#include "diagon/codecs/lucene104/ForUtil.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsWriter.h"
#include "diagon/codecs/lucene104/PForUtil.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Lucene 104 OS-compatible postings reader.
 *
 * Decodes .doc/.pos files written by Lucene104OSPostingsWriter.
 * Implements PostingsReaderBase so it can be used with BlockTreeTermsReader.
 *
 * Handles:
 * - ForUtil 256-int packed doc delta blocks
 * - PForUtil 256-int packed freq/position blocks
 * - Two-level skip data (Level0 per block, Level1 per 32 blocks)
 * - VInt tail blocks
 * - Singleton doc optimization (when used with encodeTerm/decodeTerm)
 *
 * Based on: org.apache.lucene.codecs.lucene104.Lucene104PostingsReader
 */
class Lucene104OSPostingsReader : public PostingsReaderBase {
public:
    explicit Lucene104OSPostingsReader(index::SegmentReadState& state);
    ~Lucene104OSPostingsReader() override;

    /** Initialize from terms input. Validates header and reads block size. */
    void init(store::IndexInput& termsIn);

    /**
     * Decode a term state from the term dictionary.
     * Used in Lucene-style pipeline where encodeTerm/decodeTerm is called.
     */
    void decodeTerm(store::IndexInput& termsIn, const index::FieldInfo& fieldInfo,
                    OSTermState& state, bool absolute);

    /**
     * Create a PostingsEnum using OS-compat term state.
     * Used in Lucene-style pipeline.
     */
    std::unique_ptr<index::PostingsEnum> postings(
        const index::FieldInfo& fieldInfo, const OSTermState& state, int flags);

    // ==================== PostingsReaderBase interface ====================
    // These methods accept native TermState for compatibility with BlockTreeTermsReader.

    std::unique_ptr<index::PostingsEnum> postings(
        const index::FieldInfo& fieldInfo, const TermState& termState,
        bool useBatch) override;

    std::unique_ptr<index::PostingsEnum> postingsWithPositions(
        const index::FieldInfo& fieldInfo, const TermState& termState) override;

    std::unique_ptr<index::PostingsEnum> impactsPostings(
        const index::FieldInfo& fieldInfo, const TermState& termState) override;

    void close() override;

    /** Set .doc input (for external construction). */
    void setDocInput(std::unique_ptr<store::IndexInput> docIn) { docIn_ = std::move(docIn); }

    /** Set .pos input (for external construction). */
    void setPosInput(std::unique_ptr<store::IndexInput> posIn) { posIn_ = std::move(posIn); }

private:
    std::unique_ptr<store::IndexInput> docIn_;
    std::unique_ptr<store::IndexInput> posIn_;

    ForUtil forUtil_;
    PForUtil pforUtil_;

    // Last decoded state (for delta decoding in decodeTerm)
    OSTermState lastState_;

    /** Convert native TermState to OS-compat OSTermState. */
    static OSTermState toOSTermState(const TermState& termState);
};

/**
 * PostingsEnum implementation for OS-compat format.
 *
 * Supports nextDoc() iteration through 256-block packed data
 * with two-level skip, plus VInt tail blocks.
 */
class OSPostingsEnum : public index::PostingsEnum {
public:
    OSPostingsEnum(store::IndexInput& docIn, store::IndexInput* posIn,
                   const OSTermState& state, bool readFreqs, bool readPositions);

    int nextDoc() override;
    int advance(int target) override;
    int docID() const override { return doc_; }
    int freq() const override;
    int nextPosition() override;
    int64_t cost() const override { return docFreq_; }

    static constexpr int NO_MORE_DOCS = 0x7FFFFFFF;

private:
    std::unique_ptr<store::IndexInput> docIn_;
    std::unique_ptr<store::IndexInput> posIn_;

    // Term metadata
    int docFreq_;
    int64_t totalTermFreq_;
    int singletonDocID_;

    // Flags
    bool readFreqs_;
    bool readPositions_;

    // Block state
    int32_t docDeltaBuffer_[ForUtil::BLOCK_SIZE];
    int32_t freqBuffer_[ForUtil::BLOCK_SIZE];
    int32_t posDeltaBuffer_[ForUtil::BLOCK_SIZE];
    int docBufferUpto_;
    int posBufferUpto_;

    // Current state
    int doc_;
    int freq_;
    int position_;
    int docsRead_;      // Total docs read so far for this term

    // Level0/Level1 skip state
    int level0LastDocID_;
    int level1LastDocID_;
    int64_t level0DocEndFP_;   // End of current level0 block data

    // Block encoding (must be declared after level0DocEndFP_ for init order)
    ForUtil forUtil_;
    PForUtil pforUtil_;

    /** Refill doc block from input. Returns true if more docs available. */
    bool refillDocBlock();

    /** Skip Level0 skip data. */
    void skipLevel0SkipData();

    /** Skip Level1 skip data. */
    void skipLevel1SkipData();

    /** Read VInt block (tail). */
    void readVIntBlock(int count);

    /** Refill position block. */
    void refillPosBlock();
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
