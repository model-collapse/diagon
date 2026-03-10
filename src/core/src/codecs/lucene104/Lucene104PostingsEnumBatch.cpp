// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsEnumBatch.h"

#include "diagon/util/BitPacking.h"
#include "diagon/util/SIMDPrefixSum.h"

#include <algorithm>
#include <cstring>

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104PostingsEnumBatch::Lucene104PostingsEnumBatch(std::unique_ptr<store::IndexInput> docIn,
                                                       const TermState& termState, bool writeFreqs)
    : docIn_(std::move(docIn))
    , docFreq_(termState.docFreq)
    , writeFreqs_(writeFreqs)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0)
    , docDeltaBuffer_{}
    , freqBuffer_{}
    , bufferPos_(0)
    , bufferLimit_(0) {
    // Seek to start of this term's postings
    docIn_->seek(termState.docStartFP);
}

int Lucene104PostingsEnumBatch::nextBatch(index::PostingsBatch& batch) {
    int count = 0;
    int remaining = docFreq_ - docsRead_;

    if (remaining <= 0) {
        batch.count = 0;
        return 0;
    }

    // Determine how many docs to return
    int toReturn = std::min(remaining, batch.capacity);

    // Track base doc ID for delta decoding
    int baseDoc = currentDoc_;

    while (count < toReturn) {
        // Refill buffer if needed
        if (bufferPos_ >= bufferLimit_) {
            refillBuffer();
            if (bufferLimit_ == 0) {
                break;  // No more docs available
            }
        }

        // How many docs can we take from buffer?
        int available = bufferLimit_ - bufferPos_;
        int toTake = std::min(toReturn - count, available);

        // Phase 4: SIMD-optimized delta decoding
        // Try SIMD path for aligned batches (16 or 8 docs)
        if (toTake == 16 || toTake == 8) {
            // SIMD prefix sum: convert deltas to absolute IDs in-place
            int32_t* deltaPtr = reinterpret_cast<int32_t*>(&docDeltaBuffer_[bufferPos_]);
            util::SIMDPrefixSum::prefixSum(deltaPtr, toTake, baseDoc);

            // Copy absolute IDs to output batch (SIMD-optimized memcpy)
            std::memcpy(&batch.docs[count], deltaPtr, toTake * sizeof(int32_t));

            // Copy frequencies
            if (writeFreqs_) {
                std::memcpy(&batch.freqs[count], &freqBuffer_[bufferPos_],
                            toTake * sizeof(int32_t));
            } else {
                // Fill with 1s
                for (int i = 0; i < toTake; i++) {
                    batch.freqs[count + i] = 1;
                }
            }

            // Update baseDoc to last element
            baseDoc = batch.docs[count + toTake - 1];
            bufferPos_ += toTake;
            count += toTake;
            docsRead_ += toTake;

        } else {
            // Scalar fallback for partial batches (< 8 docs)
            for (int i = 0; i < toTake; i++) {
                int docDelta = static_cast<int>(docDeltaBuffer_[bufferPos_]);

                // Convert delta to absolute doc ID
                if (baseDoc == -1) {
                    baseDoc = docDelta;  // First doc is absolute
                } else {
                    baseDoc += docDelta;
                }

                batch.docs[count] = baseDoc;
                batch.freqs[count] = writeFreqs_ ? static_cast<int>(freqBuffer_[bufferPos_]) : 1;

                bufferPos_++;
                count++;
            }
            docsRead_ += toTake;
        }
    }

    // Update current position for one-at-a-time compatibility
    if (count > 0) {
        currentDoc_ = batch.docs[count - 1];
        currentFreq_ = batch.freqs[count - 1];
    }

    batch.count = count;
    return count;
}

int Lucene104PostingsEnumBatch::nextDoc() {
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

    // Get frequency from buffer
    currentFreq_ = writeFreqs_ ? static_cast<int32_t>(freqBuffer_[bufferPos_]) : 1;

    bufferPos_++;
    docsRead_++;
    return currentDoc_;
}

void Lucene104PostingsEnumBatch::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

    static constexpr int BITPACK_BLOCK = 128;

    // Read one PFOR-Delta block if we have >= 128 remaining docs
    if (remaining >= BITPACK_BLOCK) {
        // Read token byte: (numExceptions << 5) | bitsPerValue
        uint8_t token = docIn_->readByte();
        int bpv = token & 0x1F;
        int numEx = token >> 5;

        uint8_t encoded[util::BitPacking::maxBytesPerBlock(BITPACK_BLOCK)];
        encoded[0] = token;

        if (bpv == 0 && numEx == 0) {
            // All-equal case: read VInt bytes
            int encodedPos = 1;
            while (true) {
                uint8_t b = docIn_->readByte();
                encoded[encodedPos++] = b;
                if ((b & 0x80) == 0)
                    break;
            }
        } else {
            int dataBytes = (bpv == 0) ? 0 : (BITPACK_BLOCK * bpv + 7) / 8;
            int exBytes = numEx * 2;
            if (dataBytes + exBytes > 0) {
                docIn_->readBytes(encoded + 1, dataBytes + exBytes);
            }
        }

        util::BitPacking::decode(encoded, BITPACK_BLOCK, docDeltaBuffer_);

        // Unpack freq from low bit; read non-1 freqs as VInts
        if (writeFreqs_) {
            for (int i = 0; i < BITPACK_BLOCK; i++) {
                if (docDeltaBuffer_[i] & 1) {
                    freqBuffer_[i] = 1;
                } else {
                    freqBuffer_[i] = static_cast<uint32_t>(docIn_->readVInt());
                }
                docDeltaBuffer_[i] >>= 1;
            }
        }
        bufferIdx = BITPACK_BLOCK;
    } else if (remaining > 0) {
        // VInt tail for remaining < 128 docs (same low-bit encoding)
        for (int i = 0; i < remaining; ++i) {
            uint32_t raw = static_cast<uint32_t>(docIn_->readVInt());
            if (writeFreqs_) {
                if (raw & 1) {
                    freqBuffer_[bufferIdx + i] = 1;
                } else {
                    freqBuffer_[bufferIdx + i] = static_cast<uint32_t>(docIn_->readVInt());
                }
                docDeltaBuffer_[bufferIdx + i] = raw >> 1;
            } else {
                docDeltaBuffer_[bufferIdx + i] = raw;
            }
        }
        bufferIdx += remaining;
    }

    bufferLimit_ = bufferIdx;
}

int Lucene104PostingsEnumBatch::advance(int target) {
    // Simple implementation: call nextDoc() until we reach target
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
