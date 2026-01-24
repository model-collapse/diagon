// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"

#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/Exceptions.h"

#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// File extension for doc postings
static const std::string DOC_EXTENSION = "doc";

Lucene104PostingsWriter::Lucene104PostingsWriter(index::SegmentWriteState& state)
    : docOut_(nullptr)
    , indexOptions_(index::IndexOptions::DOCS)
    , writeFreqs_(false)
    , docStartFP_(0)
    , lastDocID_(0)
    , docCount_(0)
    , totalTermFreq_(0)
    , segmentName_(state.segmentName)
    , segmentSuffix_(state.segmentSuffix) {
    // Create .doc output file
    std::string docFileName = segmentName_;
    if (!segmentSuffix_.empty()) {
        docFileName += "_" + segmentSuffix_;
    }
    docFileName += "." + DOC_EXTENSION;

    // For Phase 2 MVP, use in-memory buffer
    // TODO Phase 2.1: Use actual file via state.directory->createOutput()
    docOut_ = std::make_unique<store::ByteBuffersIndexOutput>(docFileName);
}

Lucene104PostingsWriter::~Lucene104PostingsWriter() {
    // Ensure close() was called
    if (docOut_) {
        try {
            close();
        } catch (...) {
            // Swallow exceptions in destructor
        }
    }
}

void Lucene104PostingsWriter::setField(const index::FieldInfo& fieldInfo) {
    indexOptions_ = fieldInfo.indexOptions;

    // Determine what to write based on index options
    writeFreqs_ = (indexOptions_ >= index::IndexOptions::DOCS_AND_FREQS);

    // TODO Phase 2.1: Support positions, offsets, payloads
    // bool writePositions = (indexOptions >= IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    // bool writeOffsets = (indexOptions >= IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS);
}

void Lucene104PostingsWriter::startTerm() {
    // Record file pointer at start of this term's postings
    docStartFP_ = docOut_->getFilePointer();
    lastDocID_ = 0;
    docCount_ = 0;
    totalTermFreq_ = 0;
}

void Lucene104PostingsWriter::startDoc(int docID, int freq) {
    if (docID < 0) {
        throw std::invalid_argument("docID must be >= 0");
    }

    if (docCount_ > 0 && docID <= lastDocID_) {
        throw std::invalid_argument("docs must be added in order (docID " + std::to_string(docID) +
                                    " <= lastDocID " + std::to_string(lastDocID_) + ")");
    }

    if (freq <= 0) {
        throw std::invalid_argument("freq must be > 0");
    }

    // Write doc delta (delta encoding for compression)
    int docDelta = docID - lastDocID_;
    docOut_->writeVInt(docDelta);

    // Write frequency if required
    if (writeFreqs_) {
        docOut_->writeVInt(freq);
        totalTermFreq_ += freq;
    } else {
        totalTermFreq_ = -1;  // Not tracked for DOCS_ONLY
    }

    lastDocID_ = docID;
    docCount_++;
}

TermState Lucene104PostingsWriter::finishTerm() {
    TermState state;
    state.docStartFP = docStartFP_;
    state.docFreq = docCount_;
    state.totalTermFreq = totalTermFreq_;
    state.skipOffset = -1;  // No skip list in Phase 2 MVP

    return state;
}

void Lucene104PostingsWriter::close() {
    if (docOut_) {
        docOut_->close();
        docOut_.reset();
    }
}

int64_t Lucene104PostingsWriter::getFilePointer() const {
    return docOut_ ? docOut_->getFilePointer() : 0;
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
