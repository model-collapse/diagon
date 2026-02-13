// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsEnumBatch.h"
#include "diagon/util/StreamVByte.h"
#include "diagon/util/SIMDPrefixSum.h"

#include <algorithm>
#include <cstring>

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104PostingsEnumBatch::Lucene104PostingsEnumBatch(std::unique_ptr<store::IndexInput> docIn,
                                                       const TermState& termState,
                                                       bool writeFreqs)
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
                std::memcpy(&batch.freqs[count], &freqBuffer_[bufferPos_], toTake * sizeof(int32_t));
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
                batch.freqs[count] = writeFreqs_
                    ? static_cast<int>(freqBuffer_[bufferPos_])
                    : 1;

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

    // Fill buffer with StreamVByte groups (4 docs each)
    while (remaining >= STREAMVBYTE_GROUP_SIZE &&
           bufferIdx + STREAMVBYTE_GROUP_SIZE <= BUFFER_SIZE) {
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

        // Decode 4 doc deltas using SIMD (from P0.4)
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

    // VInt fallback for remaining docs (< 4)
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
