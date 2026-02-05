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
namespace lucene105 {

/**
 * Skip entry with impact metadata for Block-Max WAND.
 *
 * Stored every 128 documents to enable early termination in top-k queries.
 */
struct SkipEntry {
    int32_t doc;           // Doc ID at start of block
    int64_t docFP;         // File pointer to doc block start
    int32_t maxFreq;       // Maximum frequency in block (128 docs)
    int8_t maxNorm;        // Maximum norm in block (0-127)

    SkipEntry() : doc(0), docFP(0), maxFreq(0), maxNorm(0) {}
};

/**
 * State for a single term's postings with impacts support.
 */
struct TermState {
    // File pointer to start of doc IDs
    int64_t docStartFP = 0;

    // File pointer to start of skip data (-1 if no skip data)
    int64_t skipStartFP = -1;

    // Document frequency (number of docs containing this term)
    int docFreq = 0;

    // Total term frequency (sum of freqs across all docs)
    int64_t totalTermFreq = 0;

    // Number of skip entries (for validation)
    int skipEntryCount = 0;
};

/**
 * Writes posting lists with impact metadata for Block-Max WAND.
 *
 * Extends Lucene104 format with:
 * - Skip entries every 128 documents
 * - Impact metadata per block (max_freq, max_norm)
 * - Separate .skp file for skip data
 *
 * This enables early termination in top-k queries by skipping blocks
 * whose maximum possible score cannot contribute to the result set.
 *
 * Based on: org.apache.lucene.codecs.lucene912.Lucene912PostingsWriter
 *
 * File format:
 * - .doc file: Same as Lucene104 (doc deltas + frequencies)
 * - .skp file (NEW): Skip entries with impacts
 *   - For each term:
 *     - numSkipEntries: VInt
 *     - For each skip entry:
 *       - docDelta: VInt (delta from previous skip entry)
 *       - docFPDelta: VLong (file pointer delta)
 *       - maxFreq: VInt (maximum frequency in next 128 docs)
 *       - maxNorm: Byte (maximum norm in next 128 docs)
 */
class Lucene105PostingsWriter {
public:
    /**
     * Constructor
     * @param state Segment write state
     */
    explicit Lucene105PostingsWriter(index::SegmentWriteState& state);

    /**
     * Destructor
     */
    ~Lucene105PostingsWriter();

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
     * @param norm Document length norm (0-127, from norms field)
     */
    void startDoc(int docID, int freq, int8_t norm = 0);

    /**
     * Finish the current term and return its state.
     * @return TermState with file pointers and skip metadata
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
    std::vector<uint8_t> getDocBytes() const;

    /**
     * Get the bytes written to the .skp file (for testing).
     * Only works with ByteBuffersIndexOutput.
     * @return Vector of bytes written
     */
    std::vector<uint8_t> getSkipBytes() const;

private:
    // Output files
    std::unique_ptr<store::IndexOutput> docOut_;  // Doc IDs and frequencies
    std::unique_ptr<store::IndexOutput> skipOut_; // Skip entries with impacts

    // Current field being written
    index::IndexOptions indexOptions_;
    bool writeFreqs_;

    // Per-term state
    int64_t docStartFP_;
    int64_t skipStartFP_;
    int lastDocID_;
    int docCount_;
    int64_t totalTermFreq_;

    // Skip list configuration
    static constexpr int SKIP_INTERVAL = 128;  // Create skip entry every 128 docs

    // Block-level impact tracking (for next skip entry)
    int32_t blockMaxFreq_;       // Max frequency in current block
    int8_t blockMaxNorm_;        // Max norm in current block
    int32_t docsSinceLastSkip_;  // Docs added since last skip entry
    int64_t lastSkipDocFP_;      // File pointer of last skip entry

    // Skip entries accumulated for current term
    std::vector<SkipEntry> skipEntries_;
    int32_t lastSkipDoc_;  // Doc ID of last skip entry (for delta encoding)

    // Segment info
    std::string segmentName_;
    std::string segmentSuffix_;

    // StreamVByte buffering
    static constexpr int BUFFER_SIZE = 4;  // StreamVByte processes 4 integers at a time
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;

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

}  // namespace lucene105
}  // namespace codecs
}  // namespace diagon
