// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"
#include "diagon/store/MMapIndexInput.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Reads posting lists written by Lucene104PostingsWriter.
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsReader
 *
 * File format (.doc file):
 * - For each term (starting at TermState.docStartFP):
 *   - for each doc:
 *     - docDelta: VInt (delta from last doc ID)
 *     - freq: VInt (term frequency, if indexed)
 */
class Lucene104PostingsReader {
public:
    /**
     * Constructor
     * @param state Segment read state
     */
    explicit Lucene104PostingsReader(index::SegmentReadState& state);

    /**
     * Destructor
     */
    ~Lucene104PostingsReader();

    /**
     * Get postings for a term.
     *
     * @param fieldInfo Field metadata
     * @param termState Term state from writer (file pointers)
     * @return PostingsEnum for iterating docs
     */
    std::unique_ptr<index::PostingsEnum> postings(const index::FieldInfo& fieldInfo,
                                                  const TermState& termState);

    /**
     * Get batch postings for a term (P1.1 optimization).
     *
     * Returns native batch implementation that can decode multiple documents
     * at once, eliminating virtual call overhead.
     *
     * @param fieldInfo Field metadata
     * @param termState Term state from writer (file pointers)
     * @param useBatch If true, return batch enum; otherwise return regular enum
     * @return PostingsEnum (may be batch-capable)
     */
    std::unique_ptr<index::PostingsEnum> postings(const index::FieldInfo& fieldInfo,
                                                  const TermState& termState, bool useBatch);

    /**
     * Get impacts-aware postings for Block-Max WAND (Phase 2).
     *
     * Returns PostingsEnum with skip entry support for early termination.
     *
     * @param fieldInfo Field metadata
     * @param termState Term state from writer (file pointers)
     * @return Impacts-aware PostingsEnum
     */
    std::unique_ptr<index::PostingsEnum> impactsPostings(const index::FieldInfo& fieldInfo,
                                                         const TermState& termState);

    /**
     * Close all input files.
     */
    void close();

    /**
     * Set input stream for testing (Phase 2 MVP).
     * In Phase 2.1, this will be done automatically in constructor.
     *
     * @param input IndexInput to read from
     */
    void setInput(std::unique_ptr<store::IndexInput> input) { docIn_ = std::move(input); }

    /**
     * Set skip input stream for testing (Phase 2).
     *
     * @param input IndexInput to read skip data from
     */
    void setSkipInput(std::unique_ptr<store::IndexInput> input) { skipIn_ = std::move(input); }

    /**
     * Read skip entries for a term from .skp file.
     *
     * @param termState Term state with skip file pointer
     * @return Vector of skip entries (empty if no skip data)
     */
    std::vector<SkipEntry> readSkipEntries(const TermState& termState);

private:
    // Input files
    std::unique_ptr<store::IndexInput> docIn_;   // Doc IDs and frequencies
    std::unique_ptr<store::IndexInput> skipIn_;  // Skip entries with impacts

    // Segment info
    std::string segmentName_;
    std::string segmentSuffix_;
};

/**
 * Impacts-aware PostingsEnum for Block-Max WAND.
 *
 * Extends PostingsEnum with impact information (max_freq, max_norm per block)
 * and advanceShallow() for efficient skip list traversal.
 *
 * Phase 2b: Block-Max WAND support
 */
class Lucene104PostingsEnumWithImpacts : public index::PostingsEnum {
public:
    /**
     * Constructor
     * @param docIn Input for reading doc IDs
     * @param termState Term state with file pointers
     * @param writeFreqs Whether frequencies are encoded
     * @param skipEntries Skip entries with impacts (from .skp file)
     */
    Lucene104PostingsEnumWithImpacts(std::unique_ptr<store::IndexInput> docIn,
                                     const TermState& termState, bool writeFreqs,
                                     const std::vector<SkipEntry>& skipEntries);

    // ==================== DocIdSetIterator ====================

    int docID() const override { return currentDoc_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return docFreq_; }

    // ==================== PostingsEnum ====================

    int freq() const override { return currentFreq_; }

    // ==================== Impacts Support ====================

    /**
     * Shallow advance to target without fully decoding.
     * Updates skip list position for accurate max score queries.
     * Returns the doc ID at the end of the current impact block
     * (i.e., the skip entry covering target), or NO_MORE_DOCS.
     *
     * @param target Target doc ID
     * @return Block boundary doc ID (inclusive)
     */
    int advanceShallow(int target);

    /**
     * Get maximum frequency in range [currentDoc, upTo].
     * Used to compute BM25 upper bounds for WAND.
     *
     * @param upTo Upper bound doc ID (inclusive)
     * @return Maximum term frequency in range
     */
    int getMaxFreq(int upTo) const;

    /**
     * Get maximum norm (encoded doc length) in range [currentDoc, upTo].
     * Used to compute BM25 upper bounds for WAND.
     *
     * @param upTo Upper bound doc ID (inclusive)
     * @return Maximum norm value (0-127, higher = shorter doc)
     */
    int getMaxNorm(int upTo) const;

    /**
     * Get both max frequency and max norm in a single pass over skip entries.
     * Avoids scanning skip entries twice (getMaxFreq + getMaxNorm separately).
     *
     * @param upTo Upper bound doc ID (inclusive)
     * @param outMaxFreq Output: maximum term frequency in range
     * @param outMaxNorm Output: maximum norm value in range
     */
    void getMaxFreqAndNorm(int upTo, int& outMaxFreq, int& outMaxNorm) const;

    /**
     * DEPRECATED: Get maximum score achievable up to upTo doc ID.
     * This method couples PostingsEnum to BM25. Use getMaxFreq/getMaxNorm instead.
     *
     * @param upTo Upper bound doc ID (inclusive)
     * @param k1 BM25 k1 parameter
     * @param b BM25 b parameter
     * @param avgFieldLength Average field length for BM25
     * @return Maximum possible score in range [currentDoc, upTo]
     */
    float getMaxScore(int upTo, float k1, float b, float avgFieldLength) const;

    /**
     * Get next block boundary after target (Phase 2: Smart upTo).
     *
     * Returns the doc ID at the start of the next block from skip metadata.
     * This allows WANDScorer to align max score updates with actual block
     * boundaries instead of using fixed 128-doc windows.
     *
     * @param target Target doc ID to search from
     * @return Next block boundary doc ID, or NO_MORE_DOCS if no more blocks
     */
    int getNextBlockBoundary(int target) const override;

    /**
     * Non-virtual batch drain: output docs+freqs from current position.
     *
     * Outputs the current doc (if valid and < upTo), then advances through
     * the internal StreamVByte buffer outputting subsequent docs.
     * After return, docID() is the first doc >= upTo, or NO_MORE_DOCS.
     *
     * Non-virtual: called through typed pointer for zero vtable overhead.
     *
     * @param upTo Upper bound (exclusive)
     * @param outDocs Output doc IDs
     * @param outFreqs Output frequencies
     * @param maxCount Max docs to output
     * @return Number of docs output
     */
    int drainBatch(int upTo, int* outDocs, int* outFreqs, int maxCount);

private:
    std::unique_ptr<store::IndexInput> docIn_;  // Owned clone for thread-safety
    store::MMapIndexInput* mmapInput_;          // Cached typed pointer (non-owning)
    int docFreq_;
    int64_t totalTermFreq_;
    bool writeFreqs_;

    // Current state
    int currentDoc_;
    int currentFreq_;
    int docsRead_;

    // Skip entries with impacts
    std::vector<SkipEntry> skipEntries_;
    int currentSkipIndex_;  // Current position in skip list
    int shallowTarget_;     // Last target passed to advanceShallow()

    // StreamVByte buffering
    static constexpr int BUFFER_SIZE = 32;
    static constexpr int STREAMVBYTE_GROUP_SIZE = 4;
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;
    int bufferLimit_;

    /**
     * Refill buffer by reading multiple StreamVByte groups (up to 32 docs).
     */
    void refillBuffer();

    /**
     * Use skip list to jump to target, avoiding full decode.
     * Returns file pointer to start reading from.
     */
    int64_t skipToTarget(int target);
};

/**
 * PostingsEnum implementation for Lucene104 format.
 *
 * Phase 2a Update: Reads StreamVByte-encoded doc deltas and frequencies for 2-3× speedup.
 * Optimized buffering: Buffers up to 32 docs (8 StreamVByte groups) to amortize decode overhead.
 * Serves docs one by one with minimal per-doc cost.
 */
class Lucene104PostingsEnum : public index::PostingsEnum {
public:
    /**
     * Constructor
     * @param docIn Input for reading doc IDs
     * @param termState Term state with file pointers
     * @param writeFreqs Whether frequencies are encoded
     */
    Lucene104PostingsEnum(std::unique_ptr<store::IndexInput> docIn, const TermState& termState,
                          bool writeFreqs);

    // ==================== DocIdSetIterator ====================

    int docID() const override { return currentDoc_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return docFreq_; }

    // ==================== PostingsEnum ====================

    int freq() const override { return currentFreq_; }

private:
    std::unique_ptr<store::IndexInput> docIn_;  // Owned clone for thread-safety
    int docFreq_;
    int64_t totalTermFreq_;
    bool writeFreqs_;

    // Current state
    int currentDoc_;
    int currentFreq_;
    int docsRead_;

    // StreamVByte buffering (Phase 2a optimized)
    // Buffer 32 docs (8 StreamVByte groups of 4) to amortize refill overhead
    static constexpr int BUFFER_SIZE = 32;
    static constexpr int STREAMVBYTE_GROUP_SIZE = 4;
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;
    int bufferLimit_;

    /**
     * Refill buffer by reading multiple StreamVByte groups (up to 32 docs).
     * Decodes groups of 4 docs each, filling up to BUFFER_SIZE.
     * Falls back to VInt for remaining docs (< 4).
     */
    void refillBuffer();
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
