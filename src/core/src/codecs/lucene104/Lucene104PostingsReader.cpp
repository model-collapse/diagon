// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h"
#include "diagon/codecs/lucene104/Lucene104PostingsEnumBatch.h"

#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/util/Exceptions.h"
#include "diagon/util/StreamVByte.h"

#include <iostream>

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

    // Use optimized version with SIMD StreamVByte decoding
    return std::make_unique<Lucene104PostingsEnumOptimized>(docIn_.get(), termState, writeFreqs);
}

std::unique_ptr<index::PostingsEnum>
Lucene104PostingsReader::postings(const index::FieldInfo& fieldInfo, const TermState& termState,
                                  bool useBatch) {
    if (!docIn_) {
        throw std::runtime_error("No input set for PostingsReader");
    }

    // Determine if frequencies are written
    bool writeFreqs = (fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS);

    if (useBatch) {
        // P1.1: Return native batch implementation
        return std::make_unique<Lucene104PostingsEnumBatch>(docIn_.get(), termState, writeFreqs);
    } else {
        // Return regular optimized version
        return std::make_unique<Lucene104PostingsEnumOptimized>(docIn_.get(), termState, writeFreqs);
    }
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
    , docsRead_(0)
    , docDeltaBuffer_{}  // Zero-initialize
    , freqBuffer_{}      // Zero-initialize
    , bufferPos_(0)
    , bufferLimit_(0) {
    // Seek to start of this term's postings
    docIn_->seek(termState.docStartFP);
}

int Lucene104PostingsEnum::nextDoc() {
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

void Lucene104PostingsEnum::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

    // Fill buffer with as many complete StreamVByte groups (4 docs each) as possible
    while (remaining >= STREAMVBYTE_GROUP_SIZE && bufferIdx + STREAMVBYTE_GROUP_SIZE <= BUFFER_SIZE) {
        // Read StreamVByte-encoded group of 4 docs
        uint8_t docDeltaEncoded[17];  // Max: 1 control + 4*4 data bytes
        uint8_t controlByte = docIn_->readByte();
        docDeltaEncoded[0] = controlByte;

        // Calculate data bytes needed from control byte
        int dataBytes = 0;
        for (int i = 0; i < 4; ++i) {
            int length = ((controlByte >> (i * 2)) & 0x03) + 1;
            dataBytes += length;
        }

        // Read data bytes
        docIn_->readBytes(docDeltaEncoded + 1, dataBytes);

        // Decode 4 doc deltas directly into buffer at current position
        util::StreamVByte::decode4(docDeltaEncoded, &docDeltaBuffer_[bufferIdx]);

        // Read frequencies if present
        if (writeFreqs_) {
            uint8_t freqEncoded[17];
            controlByte = docIn_->readByte();
            freqEncoded[0] = controlByte;

            // Calculate data bytes for frequencies
            dataBytes = 0;
            for (int i = 0; i < 4; ++i) {
                int length = ((controlByte >> (i * 2)) & 0x03) + 1;
                dataBytes += length;
            }

            docIn_->readBytes(freqEncoded + 1, dataBytes);
            util::StreamVByte::decode4(freqEncoded, &freqBuffer_[bufferIdx]);
        }

        bufferIdx += STREAMVBYTE_GROUP_SIZE;
        remaining -= STREAMVBYTE_GROUP_SIZE;
    }

    // Use VInt fallback for remaining docs (< 4), but only if there's buffer space
    int spaceLeft = BUFFER_SIZE - bufferIdx;
    int docsToRead = std::min(remaining, spaceLeft);

    if (docsToRead > 0) {
        for (int i = 0; i < docsToRead; ++i) {
            docDeltaBuffer_[bufferIdx + i] = static_cast<uint32_t>(docIn_->readVInt());
            if (writeFreqs_) {
                freqBuffer_[bufferIdx + i] = static_cast<uint32_t>(docIn_->readVInt());
            }
        }
        bufferIdx += docsToRead;
    }

    bufferLimit_ = bufferIdx;
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
