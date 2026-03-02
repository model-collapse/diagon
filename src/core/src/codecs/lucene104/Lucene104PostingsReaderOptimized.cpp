// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h"

#include "diagon/util/BitPacking.h"
#include "diagon/util/Exceptions.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

static constexpr int BITPACK_BLOCK = 128;

Lucene104PostingsEnumOptimized::Lucene104PostingsEnumOptimized(
    std::unique_ptr<store::IndexInput> docIn, const TermState& termState, bool writeFreqs)
    : docIn_(std::move(docIn))
    , docFreq_(termState.docFreq)
    , totalTermFreq_(termState.totalTermFreq)
    , writeFreqs_(writeFreqs)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0)
    , docDeltaBuffer_{}
    , freqBuffer_{}
    , bufferPos_(0)
    , bufferLimit_(0)
    , ioBatch_{}
    , ioBatchPos_(0)
    , ioBatchLimit_(0) {
    docIn_->seek(termState.docStartFP);
    refillIOBatch();
}

int Lucene104PostingsEnumOptimized::nextDoc() {
    if (docsRead_ >= docFreq_) {
        currentDoc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    if (bufferPos_ >= bufferLimit_) {
        refillBuffer();
    }

    int docDelta = static_cast<int32_t>(docDeltaBuffer_[bufferPos_]);
    if (currentDoc_ == -1) {
        currentDoc_ = docDelta;
    } else {
        currentDoc_ += docDelta;
    }
    currentFreq_ = writeFreqs_ ? static_cast<int32_t>(freqBuffer_[bufferPos_]) : 1;

    bufferPos_++;
    docsRead_++;
    return currentDoc_;
}

void Lucene104PostingsEnumOptimized::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

    if (remaining >= BITPACK_BLOCK) {
        // Read PFOR-Delta block from I/O batch
        uint8_t token = readByteFromBatch();
        int bpv = token & 0x1F;
        int numEx = token >> 5;

        uint8_t encoded[util::BitPacking::maxBytesPerBlock(BITPACK_BLOCK)];
        encoded[0] = token;

        if (bpv == 0 && numEx == 0) {
            // All-equal case: read VInt bytes
            int encodedPos = 1;
            while (true) {
                uint8_t b = readByteFromBatch();
                encoded[encodedPos++] = b;
                if ((b & 0x80) == 0) break;
            }
        } else {
            int dataBytes = (bpv == 0) ? 0 : (BITPACK_BLOCK * bpv + 7) / 8;
            int exBytes = numEx * 2;
            if (dataBytes + exBytes > 0) {
                readBytesFromBatch(encoded + 1, dataBytes + exBytes);
            }
        }

        util::BitPacking::decode(encoded, BITPACK_BLOCK, docDeltaBuffer_);

        // Unpack freq from low bit; read non-1 freqs as VInts from I/O batch
        if (writeFreqs_) {
            for (int i = 0; i < BITPACK_BLOCK; i++) {
                if (docDeltaBuffer_[i] & 1) {
                    freqBuffer_[i] = 1;
                } else {
                    freqBuffer_[i] = static_cast<uint32_t>(readVIntFromBatch());
                }
                docDeltaBuffer_[i] >>= 1;
            }
        }
        bufferIdx = BITPACK_BLOCK;
    } else if (remaining > 0) {
        // VInt tail for remaining < 128 docs
        for (int i = 0; i < remaining; ++i) {
            uint32_t raw = static_cast<uint32_t>(readVIntFromBatch());
            if (writeFreqs_) {
                if (raw & 1) {
                    freqBuffer_[bufferIdx + i] = 1;
                } else {
                    freqBuffer_[bufferIdx + i] = static_cast<uint32_t>(readVIntFromBatch());
                }
                docDeltaBuffer_[bufferIdx + i] = raw >> 1;
            } else {
                docDeltaBuffer_[bufferIdx + i] = raw;
                freqBuffer_[bufferIdx + i] = 1;
            }
        }
        bufferIdx += remaining;
    }

    bufferLimit_ = bufferIdx;
}

int Lucene104PostingsEnumOptimized::advance(int target) {
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
