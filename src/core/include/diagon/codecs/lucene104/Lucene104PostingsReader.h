// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"

#include <memory>
#include <vector>
#include <cstdint>

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
    std::unique_ptr<index::PostingsEnum> postings(
        const index::FieldInfo& fieldInfo,
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
    void setInput(std::unique_ptr<store::IndexInput> input) {
        docIn_ = std::move(input);
    }

private:
    // Input file for doc IDs and frequencies
    std::unique_ptr<store::IndexInput> docIn_;

    // Segment info
    std::string segmentName_;
    std::string segmentSuffix_;
};

/**
 * PostingsEnum implementation for Lucene104 format.
 *
 * Reads VByte-encoded doc deltas and frequencies.
 */
class Lucene104PostingsEnum : public index::PostingsEnum {
public:
    /**
     * Constructor
     * @param docIn Input for reading doc IDs
     * @param termState Term state with file pointers
     * @param writeFreqs Whether frequencies are encoded
     */
    Lucene104PostingsEnum(store::IndexInput* docIn,
                          const TermState& termState,
                          bool writeFreqs);

    // ==================== DocIdSetIterator ====================

    int docID() const override {
        return currentDoc_;
    }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override {
        return docFreq_;
    }

    // ==================== PostingsEnum ====================

    int freq() const override {
        return currentFreq_;
    }

private:
    store::IndexInput* docIn_;  // Not owned
    int docFreq_;
    int64_t totalTermFreq_;
    bool writeFreqs_;

    // Current state
    int currentDoc_;
    int currentFreq_;
    int docsRead_;
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
