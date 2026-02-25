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
 * Skip entry with impact metadata for Block-Max WAND.
 *
 * Stored every 128 documents to enable early termination in top-k queries.
 */
struct SkipEntry {
    int32_t doc;      // Doc ID at start of block
    int64_t docFP;    // File pointer to doc block start
    int32_t maxFreq;  // Maximum frequency in block (128 docs)
    int8_t maxNorm;   // Maximum norm in block (0-127)

    SkipEntry()
        : doc(0)
        , docFP(0)
        , maxFreq(0)
        , maxNorm(0) {}
};

/**
 * State for a single term's postings.
 *
 * Stores file pointers and metadata needed to read the postings back.
 */
struct TermState {
    // File pointer to start of doc IDs
    int64_t docStartFP = 0;

    // File pointer to start of position data (-1 = no positions)
    int64_t posStartFP = -1;

    // Document frequency (number of docs containing this term)
    int docFreq = 0;

    // Total term frequency (sum of freqs across all docs)
    int64_t totalTermFreq = 0;

    // Block-Max WAND support (optional, backward compatible)
    // If skipStartFP == -1, no skip/impact data exists (small postings or old format)
    int64_t skipStartFP = -1;  // File pointer to skip data in .skp file
    int skipEntryCount = 0;    // Number of skip entries
};

/**
 * Writes posting lists for a single field using Lucene104 format.
 *
 * Supports:
 * - StreamVByte encoding for doc deltas and frequencies (groups of 4)
 * - SIMD-accelerated encoding with control bytes
 * - Block-Max WAND impacts (optional, enabled for postings >= 128 docs)
 *
 * Based on: org.apache.lucene.codecs.lucene912.Lucene912PostingsWriter
 *
 * File format:
 * - .doc file: Doc deltas and frequencies (StreamVByte encoded)
 *   - For each term:
 *     - for each group of 4 docs:
 *       - controlByte: uint8 (2 bits per integer length)
 *       - docDeltas: 4-16 bytes (delta-encoded doc IDs)
 *       - freqs: 4-16 bytes (term frequencies, if indexed)
 *     - remaining docs (< 4): VInt fallback
 *
 * - .skp file (optional): Skip entries with impacts for Block-Max WAND
 *   - For each term (if docFreq >= 128):
 *     - numSkipEntries: VInt
 *     - For each skip entry (every 128 docs):
 *       - docDelta: VInt (delta from previous)
 *       - docFPDelta: VLong (file pointer delta)
 *       - maxFreq: VInt (max frequency in block)
 *       - maxNorm: Byte (max norm in block, 0-127)
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
     * @param norm Document length norm (0-127, optional, for impacts)
     */
    void startDoc(int docID, int freq, int8_t norm = 0);

    /**
     * Add a position for the current document.
     * Must be called after startDoc() and before the next startDoc().
     * Only writes when field has positions indexed.
     * @param position Term position in document
     */
    void addPosition(int position);

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

    /**
     * Get the bytes written to the .doc file (for testing).
     * Only works with ByteBuffersIndexOutput.
     * @return Vector of bytes written
     */
    std::vector<uint8_t> getBytes() const;

    /**
     * Get accumulated skip bytes from in-memory buffer.
     * @return Vector of skip data bytes
     */
    std::vector<uint8_t> getSkipBytes() const;

    /**
     * Get the bytes written to the .pos file (for testing).
     * Only works with ByteBuffersIndexOutput.
     * @return Vector of bytes written
     */
    std::vector<uint8_t> getPositionBytes() const;

    /**
     * Get skip file name.
     * @return Skip file name
     */
    std::string getSkipFileName() const { return skipFileName_; }

    /**
     * Get position file name.
     * @return Position file name
     */
    std::string getPosFileName() const { return posFileName_; }

private:
    // Output files
    std::unique_ptr<store::IndexOutput> docOut_;   // Doc IDs and frequencies
    std::unique_ptr<store::IndexOutput> skipOut_;  // Skip entries with impacts (optional)
    std::unique_ptr<store::IndexOutput> posOut_;   // Position data (optional)

    // Current field being written
    index::IndexOptions indexOptions_;
    bool writeFreqs_;
    bool writePositions_;

    // Per-term state
    int64_t docStartFP_;
    int64_t skipStartFP_;
    int lastDocID_;
    int docCount_;
    int64_t totalTermFreq_;

    // Segment info
    [[maybe_unused]] store::Directory* directory_;  // Directory for writing files
    std::string segmentName_;
    std::string segmentSuffix_;
    std::string docFileName_;   // Full .doc file name
    std::string skipFileName_;  // Full .skp file name
    std::string posFileName_;   // Full .pos file name

    // Per-term position state
    int64_t posStartFP_;  // File pointer at start of positions for current term
    int lastPosition_;    // Last position written (for delta encoding within a doc)

    // StreamVByte buffering
    static constexpr int BUFFER_SIZE = 4;  // StreamVByte processes 4 integers at a time
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;

    // Block-Max WAND support
    // Lowered from 256 to 64 to create more skip entries for tighter max score bounds
    static constexpr int SKIP_INTERVAL =
        64;  // Create skip entry every 64 docs (denser than Lucene)

    // Block-level impact tracking (for next skip entry)
    int32_t blockMaxFreq_;       // Max frequency in current block
    int8_t blockMaxNorm_;        // Max norm in current block
    int32_t docsSinceLastSkip_;  // Docs added since last skip entry
    int64_t lastSkipDocFP_;      // File pointer of last skip entry

    // Skip entries accumulated for current term
    std::vector<SkipEntry> skipEntries_;
    int32_t lastSkipDoc_;  // Doc ID of last skip entry (for delta encoding)

    /**
     * Flush buffered doc deltas and frequencies using StreamVByte encoding.
     * Called when buffer fills or at end of term.
     */
    void flushBuffer();

    /**
     * Check if we need to create a skip entry and do so if needed.
     */
    void maybeFlushSkipEntry();

    /**
     * Write all accumulated skip entries for current term to .skp file.
     */
    void writeSkipData();
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
