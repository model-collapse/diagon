// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexOutput.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/FieldInfo.h"

#include <memory>
#include <vector>
#include <cstdint>

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
 * Simplified implementation for Phase 2:
 * - VByte delta encoding for doc IDs
 * - VByte encoding for term frequencies
 * - Single .doc file (no .pos, .pay files yet)
 * - Skip lists deferred to Phase 2.1
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsWriter
 *
 * File format (.doc file):
 * - For each term:
 *   - docFreq: VInt
 *   - totalTermFreq: VLong
 *   - for each doc:
 *     - docDelta: VInt (delta from last doc ID)
 *     - freq: VInt (term frequency in this doc)
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
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
