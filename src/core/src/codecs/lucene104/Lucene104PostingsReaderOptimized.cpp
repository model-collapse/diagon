// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h"

#include "diagon/util/Exceptions.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104PostingsEnumOptimized::Lucene104PostingsEnumOptimized(store::IndexInput* docIn,
                                                               const TermState& termState,
                                                               bool writeFreqs)
    : docIn_(docIn)
    , docFreq_(termState.docFreq)
    , totalTermFreq_(termState.totalTermFreq)
    , writeFreqs_(writeFreqs)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0)
    , docDeltaBuffer_{}  // Zero-initialize
    , freqBuffer_{}      // Zero-initialize
    , bufferPos_(0)
    , bufferLimit_(0)
    , ioBatch_{}  // Zero-initialize
    , ioBatchPos_(0)
    , ioBatchLimit_(0) {
    // Seek to start of this term's postings
    docIn_->seek(termState.docStartFP);

    // Pre-fill I/O batch buffer
    refillIOBatch();
}

int Lucene104PostingsEnumOptimized::nextDoc() {
    if (docsRead_ >= docFreq_) {
        currentDoc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    // Refill buffer if empty
    if (bufferPos_ >= bufferLimit_) {
        refillBuffer();
    }

    // Get doc delta from buffer
    int docDelta = static_cast<int32_t>(docDeltaBuffer_[bufferPos_]);

    // Update current doc (delta encoding)
    if (currentDoc_ == -1) {
        currentDoc_ = docDelta;  // First doc is absolute
    } else {
        currentDoc_ += docDelta;
    }

    // Get frequency from buffer (branchless using multiplication)
    currentFreq_ = writeFreqs_ ? static_cast<int32_t>(freqBuffer_[bufferPos_]) : 1;

    bufferPos_++;
    docsRead_++;
    return currentDoc_;
}

void Lucene104PostingsEnumOptimized::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

    // Fill buffer with as many complete StreamVByte groups (4 docs each) as possible
    // Process in batches for better cache locality
    while (remaining >= STREAMVBYTE_GROUP_SIZE && bufferIdx + STREAMVBYTE_GROUP_SIZE <= BUFFER_SIZE) {
        // Inline StreamVByte decode for doc deltas
        decodeStreamVByte4(&docDeltaBuffer_[bufferIdx]);

        // Inline StreamVByte decode for frequencies
        if (writeFreqs_) {
            decodeStreamVByte4(&freqBuffer_[bufferIdx]);
        } else {
            // Set default frequencies
            freqBuffer_[bufferIdx] = 1;
            freqBuffer_[bufferIdx + 1] = 1;
            freqBuffer_[bufferIdx + 2] = 1;
            freqBuffer_[bufferIdx + 3] = 1;
        }

        bufferIdx += STREAMVBYTE_GROUP_SIZE;
        remaining -= STREAMVBYTE_GROUP_SIZE;
    }

    // Use VInt fallback for remaining docs (< 4)
    int spaceLeft = BUFFER_SIZE - bufferIdx;
    int docsToRead = std::min(remaining, spaceLeft);

    if (docsToRead > 0) {
        // Read VInt from batch buffer (not directly from file)
        for (int i = 0; i < docsToRead; ++i) {
            docDeltaBuffer_[bufferIdx + i] = static_cast<uint32_t>(readVIntFromBatch());
            if (writeFreqs_) {
                freqBuffer_[bufferIdx + i] = static_cast<uint32_t>(readVIntFromBatch());
            } else {
                freqBuffer_[bufferIdx + i] = 1;
            }
        }
        bufferIdx += docsToRead;
    }

    bufferLimit_ = bufferIdx;
}

int Lucene104PostingsEnumOptimized::advance(int target) {
    // Simple implementation: just call nextDoc() until we reach target
    // TODO: Use skip lists for efficient advance()
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
