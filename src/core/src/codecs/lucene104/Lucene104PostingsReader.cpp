// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"

#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/util/Exceptions.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

// File extension for doc postings
static const std::string DOC_EXTENSION = "doc";

Lucene104PostingsReader::Lucene104PostingsReader(index::SegmentReadState& state)
    : docIn_(nullptr)
    , segmentName_(state.segmentName)
    , segmentSuffix_(state.segmentSuffix) {
    // Create .doc input file
    std::string docFileName = segmentName_;
    if (!segmentSuffix_.empty()) {
        docFileName += "_" + segmentSuffix_;
    }
    docFileName += "." + DOC_EXTENSION;

    // For Phase 2 MVP, docIn_ will be set externally via setInput()
    // TODO Phase 2.1: Use actual file via state.directory->openInput()
}

Lucene104PostingsReader::~Lucene104PostingsReader() {
    if (docIn_) {
        try {
            close();
        } catch (...) {
            // Swallow exceptions in destructor
        }
    }
}

std::unique_ptr<index::PostingsEnum>
Lucene104PostingsReader::postings(const index::FieldInfo& fieldInfo, const TermState& termState) {
    if (!docIn_) {
        throw std::runtime_error("No input set for PostingsReader");
    }

    // Determine if frequencies are written
    bool writeFreqs = (fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS);

    return std::make_unique<Lucene104PostingsEnum>(docIn_.get(), termState, writeFreqs);
}

void Lucene104PostingsReader::close() {
    if (docIn_) {
        docIn_.reset();
    }
}

// ==================== Lucene104PostingsEnum ====================

Lucene104PostingsEnum::Lucene104PostingsEnum(store::IndexInput* docIn, const TermState& termState,
                                             bool writeFreqs)
    : docIn_(docIn)
    , docFreq_(termState.docFreq)
    , totalTermFreq_(termState.totalTermFreq)
    , writeFreqs_(writeFreqs)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0) {
    // Seek to start of this term's postings
    docIn_->seek(termState.docStartFP);
}

int Lucene104PostingsEnum::nextDoc() {
    if (docsRead_ >= docFreq_) {
        currentDoc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    // Read doc delta (VInt encoded)
    int docDelta = docIn_->readVInt();

    // Update current doc (delta encoding)
    if (currentDoc_ == -1) {
        currentDoc_ = docDelta;  // First doc is absolute
    } else {
        currentDoc_ += docDelta;
    }

    // Read frequency if present
    if (writeFreqs_) {
        currentFreq_ = docIn_->readVInt();
    } else {
        currentFreq_ = 1;  // Default for DOCS_ONLY
    }

    docsRead_++;
    return currentDoc_;
}

int Lucene104PostingsEnum::advance(int target) {
    // Simple implementation: just call nextDoc() until we reach target
    // TODO Phase 2.1: Use skip lists for efficient advance()
    while (currentDoc_ < target) {
        if (nextDoc() == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }
    }
    return currentDoc_;
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
