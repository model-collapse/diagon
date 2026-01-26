// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

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
 * State for a single term's postings.
 *
 * Stores file pointers and metadata needed to read the postings back.
 */
struct TermState {
    // File pointer to start of doc IDs
    int64_t docStartFP = 0;

    // Document frequency (number of docs containing this term)
    int docFreq = 0;

    // Total term frequency (sum of freqs across all docs)
    int64_t totalTermFreq = 0;

    // For skip list support (Phase 2.1)
    int64_t skipOffset = -1;
};

/**
 * Writes posting lists for a single field using Lucene104 format.
 *
 * Phase 2a Update: StreamVByte encoding for 2-3× speedup
 * - StreamVByte encoding for doc deltas and frequencies (groups of 4)
 * - SIMD-accelerated encoding with control bytes
 * - Single .doc file (no .pos, .pay files yet)
 * - Skip lists deferred to Phase 2.1
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsWriter
 *
 * File format (.doc file):
 * - For each term:
 *   - docFreq: VInt (metadata, not StreamVByte)
 *   - totalTermFreq: VLong (metadata, not StreamVByte)
 *   - for each group of 4 docs:
 *     - controlByte: uint8 (2 bits per integer length)
 *     - docDeltas: 4-16 bytes (delta-encoded doc IDs, StreamVByte format)
 *     - freqs: 4-16 bytes (term frequencies, StreamVByte format, if indexed)
 *   - remaining docs (< 4): VInt fallback
 */
class Lucene104PostingsWriter {
public:
    /**
     * Constructor
     * @param state Segment write state
     */
    explicit Lucene104PostingsWriter(index::SegmentWriteState& state);

    /**
     * Destructor
     */
    ~Lucene104PostingsWriter();

    /**
     * Start writing a new field.
     * @param fieldInfo Field metadata
     */
    void setField(const index::FieldInfo& fieldInfo);

    /**
     * Start a new term.
     * Must call this before startDoc().
     */
    void startTerm();

    /**
     * Add a document to current term's postings.
     * @param docID Document ID (must be in ascending order)
     * @param freq Term frequency in this document
     */
    void startDoc(int docID, int freq);

    /**
     * Finish the current term and return its state.
     * @return TermState with file pointers
     */
    TermState finishTerm();

    /**
     * Close all output files.
     */
    void close();

    /**
     * Get current file pointer in .doc file.
     * @return File pointer position
     */
    int64_t getFilePointer() const;

private:
    // Output file for doc IDs and frequencies
    std::unique_ptr<store::IndexOutput> docOut_;

    // Current field being written
    index::IndexOptions indexOptions_;
    bool writeFreqs_;

    // Per-term state
    int64_t docStartFP_;
    int lastDocID_;
    int docCount_;
    int64_t totalTermFreq_;

    // Segment info
    std::string segmentName_;
    std::string segmentSuffix_;

    // StreamVByte buffering (Phase 2a)
    static constexpr int BUFFER_SIZE = 4;  // StreamVByte processes 4 integers at a time
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;

    /**
     * Flush buffered doc deltas and frequencies using StreamVByte encoding.
     * Called when buffer fills or at end of term.
     */
    void flushBuffer();
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
