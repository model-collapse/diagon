// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/ForUtil.h"
#include "diagon/codecs/lucene104/PForUtil.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Term state for OS-compat postings format.
 * Matches Lucene104PostingsFormat.IntBlockTermState.
 */
struct OSTermState {
    int64_t docStartFP = 0;
    int64_t posStartFP = -1;
    int64_t payStartFP = -1;
    int64_t lastPosBlockOffset = -1;
    int singletonDocID = -1;
    int docFreq = 0;
    int64_t totalTermFreq = 0;

    // For skip data in native format (not used in OS-compat, but kept for API compat)
    int64_t skipStartFP = -1;
};

/**
 * Lucene 104 OS-compatible postings writer.
 *
 * Produces .doc/.pos files byte-level compatible with OpenSearch/Lucene:
 * - Block size 256 (matching Lucene 104)
 * - ForUtil for doc deltas (no patching)
 * - PForUtil for frequencies and positions (with patching)
 * - Two-level skip data: Level0 per block, Level1 per 32 blocks
 * - VInt tail blocks for remaining docs < 256
 * - Competitive impacts for Block-Max WAND
 *
 * Wire format:
 *   .doc: IndexHeader + {Level1Skip? + {Level0Skip + ForUtil(docDeltas) + PForUtil(freqs)}*32}* + VIntTail + Footer
 *   .pos: IndexHeader + {PForUtil(posDeltas)}* + VIntTail + Footer
 *
 * Based on: org.apache.lucene.codecs.lucene104.Lucene104PostingsWriter
 */
class Lucene104OSPostingsWriter {
public:
    static constexpr int BLOCK_SIZE = ForUtil::BLOCK_SIZE;  // 256
    static constexpr int LEVEL1_FACTOR = 32;
    static constexpr int LEVEL1_NUM_DOCS = LEVEL1_FACTOR * BLOCK_SIZE;  // 8192
    static constexpr int LEVEL1_MASK = LEVEL1_NUM_DOCS - 1;

    // Codec names matching Lucene 104 exactly
    static constexpr const char* DOC_CODEC = "Lucene104PostingsWriterDoc";
    static constexpr const char* POS_CODEC = "Lucene104PostingsWriterPos";
    static constexpr const char* META_CODEC = "Lucene104PostingsWriterMeta";
    static constexpr const char* TERMS_CODEC = "Lucene104PostingsWriterTerms";
    static constexpr int VERSION_CURRENT = 0;

    explicit Lucene104OSPostingsWriter(index::SegmentWriteState& state);
    ~Lucene104OSPostingsWriter();

    /** Initialize terms output (write TERMS_CODEC header + block size). */
    void init(store::IndexOutput& termsOut);

    /** Set current field. */
    void setField(const index::FieldInfo& fieldInfo);

    /** Start a new term. */
    void startTerm();

    /** Add a document with frequency. */
    void startDoc(int docID, int freq);

    /** Add a position for current document. */
    void addPosition(int position);

    /** Finish current document. */
    void finishDoc();

    /** Finish current term, return state. */
    OSTermState finishTerm();

    /** Encode term metadata to term dictionary. */
    void encodeTerm(store::IndexOutput& out, const OSTermState& state, bool absolute);

    /** Close all output files, write metadata. */
    void close();

    /** Get .doc file name. */
    const std::string& getDocFileName() const { return docFileName_; }

    /** Get .pos file name. */
    const std::string& getPosFileName() const { return posFileName_; }

private:
    // Output files
    std::unique_ptr<store::IndexOutput> docOut_;
    std::unique_ptr<store::IndexOutput> posOut_;
    std::unique_ptr<store::IndexOutput> metaOut_;

    // Encoding engines
    ForUtil forUtil_;
    PForUtil pforUtil_;

    // Field state
    index::IndexOptions indexOptions_;
    bool writeFreqs_;
    bool writePositions_;
    bool fieldHasNorms_;

    // Per-term state
    int64_t docStartFP_;
    int64_t posStartFP_;
    int lastDocID_;
    int docID_;
    int docCount_;
    int lastPosition_;

    // Level0/Level1 skip tracking
    int level0LastDocID_;
    int64_t level0LastPosFP_;
    int level1LastDocID_;
    int64_t level1LastPosFP_;

    // Doc buffer (256 entries)
    int32_t docDeltaBuffer_[BLOCK_SIZE];
    int32_t freqBuffer_[BLOCK_SIZE];
    int docBufferUpto_;

    // Position buffer (256 entries)
    int32_t posDeltaBuffer_[BLOCK_SIZE];
    int posBufferUpto_;

    // In-memory scratch buffers for skip data
    // Level0: single block's skip data + packed data
    std::vector<uint8_t> level0Buf_;
    // Level1: 32 blocks' combined output
    std::vector<uint8_t> level1Buf_;
    // Scratch for impacts/skip length prefix
    std::vector<uint8_t> scratchBuf_;

    // Metadata tracking
    int maxNumImpactsAtLevel0_;
    int maxImpactNumBytesAtLevel0_;
    int maxNumImpactsAtLevel1_;
    int maxImpactNumBytesAtLevel1_;

    // Competitive impact accumulator (simplified for Diagon)
    // Stores (freq, norm) pairs for Block-Max WAND
    struct Impact {
        int freq;
        int64_t norm;
    };
    std::vector<Impact> level0Impacts_;
    std::vector<Impact> level1Impacts_;

    // State for encodeTerm
    OSTermState lastEncodedState_;

    // File names
    std::string segmentName_;
    std::string docFileName_;
    std::string posFileName_;
    std::string metaFileName_;

    // Reference to segment state
    index::SegmentWriteState& state_;

    /** Flush a doc block (called when buffer fills or term finishes). */
    void flushDocBlock(bool finishTerm);

    /** Write Level1 skip data and flush level1 buffer to docOut. */
    void writeLevel1SkipData();

    /** Write competitive impacts to a buffer. */
    static void writeImpacts(const std::vector<Impact>& impacts, std::vector<uint8_t>& out);

    /** Write VInt to byte buffer. */
    static void writeVIntToBuffer(std::vector<uint8_t>& buf, int32_t v);

    /** Write VLong to byte buffer. */
    static void writeVLongToBuffer(std::vector<uint8_t>& buf, int64_t v);

    /** Write VInt15 (2-byte optimized) to byte buffer. */
    static void writeVInt15ToBuffer(std::vector<uint8_t>& buf, int32_t v);

    /** Write VLong15 (2-byte optimized) to byte buffer. */
    static void writeVLong15ToBuffer(std::vector<uint8_t>& buf, int64_t v);

    /** Write VInt block (tail docs) using GroupVInt-style encoding. */
    void writeVIntBlock(store::IndexOutput& out, int32_t* docDeltas, int32_t* freqs,
                        int count, bool writeFreqs);

    /** Add impact to accumulator. */
    void addImpact(std::vector<Impact>& acc, int freq, int64_t norm);

    /** Get sorted, deduplicated competitive impacts. */
    static std::vector<Impact> getCompetitiveImpacts(const std::vector<Impact>& acc);
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
