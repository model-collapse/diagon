// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Optimized PostingsEnum implementation for Lucene104 format.
 *
 * Optimizations over Lucene104PostingsEnum:
 * 1. Inlined StreamVByte decoding (eliminates function call overhead)
 * 2. Larger buffer (128 docs vs 32) for better amortization
 * 3. Batch I/O - read larger chunks at once
 * 4. Optimized control byte interpretation
 * 5. Prefetching hints for better cache performance
 *
 * Target: >100 M items/sec (vs current 39 M/s = 2.6x improvement)
 */
class Lucene104PostingsEnumOptimized : public index::PostingsEnum {
public:
    /**
     * Constructor
     * @param docIn Input for reading doc IDs
     * @param termState Term state with file pointers
     * @param writeFreqs Whether frequencies are encoded
     */
    Lucene104PostingsEnumOptimized(store::IndexInput* docIn, const TermState& termState,
                                   bool writeFreqs);

    // ==================== DocIdSetIterator ====================

    int docID() const override { return currentDoc_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return docFreq_; }

    // ==================== PostingsEnum ====================

    int freq() const override { return currentFreq_; }

private:
    store::IndexInput* docIn_;  // Not owned
    int docFreq_;
    int64_t totalTermFreq_;
    bool writeFreqs_;

    // Current state
    int currentDoc_;
    int currentFreq_;
    int docsRead_;

    // Optimized buffering: 128 docs (32 StreamVByte groups of 4)
    // Larger buffer = better amortization of refill overhead
    static constexpr int BUFFER_SIZE = 128;
    static constexpr int STREAMVBYTE_GROUP_SIZE = 4;
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;
    int bufferLimit_;

    // I/O batch buffer - read larger chunks to reduce syscall overhead
    static constexpr int IO_BATCH_SIZE = 512;  // Read 512 bytes at a time
    uint8_t ioBatch_[IO_BATCH_SIZE];
    int ioBatchPos_;
    int ioBatchLimit_;

    /**
     * Refill buffer with optimized batched I/O and inlined decoding
     */
    void refillBuffer();

    /**
     * Refill I/O batch buffer
     */
    inline void refillIOBatch() {
        int64_t remaining = docIn_->length() - docIn_->getFilePointer();
        int toRead = static_cast<int>(std::min(static_cast<int64_t>(IO_BATCH_SIZE), remaining));
        if (toRead > 0) {
            docIn_->readBytes(ioBatch_, toRead);
            ioBatchPos_ = 0;
            ioBatchLimit_ = toRead;
        } else {
            ioBatchLimit_ = 0;
        }
    }

    /**
     * Read byte from I/O batch buffer
     */
    inline uint8_t readByteFromBatch() {
        if (ioBatchPos_ >= ioBatchLimit_) {
            refillIOBatch();
        }
        return ioBatch_[ioBatchPos_++];
    }

    /**
     * Inlined StreamVByte decode for 4 integers
     * Eliminates function call overhead
     */
    inline void decodeStreamVByte4(uint32_t* output) {
        // Read control byte
        uint8_t control = readByteFromBatch();

        // Decode lengths from control byte (2 bits per integer)
        int len0 = (control & 0x03) + 1;
        int len1 = ((control >> 2) & 0x03) + 1;
        int len2 = ((control >> 4) & 0x03) + 1;
        int len3 = ((control >> 6) & 0x03) + 1;

        // Decode value 0
        output[0] = 0;
        for (int j = 0; j < len0; j++) {
            output[0] |= static_cast<uint32_t>(readByteFromBatch()) << (j * 8);
        }

        // Decode value 1
        output[1] = 0;
        for (int j = 0; j < len1; j++) {
            output[1] |= static_cast<uint32_t>(readByteFromBatch()) << (j * 8);
        }

        // Decode value 2
        output[2] = 0;
        for (int j = 0; j < len2; j++) {
            output[2] |= static_cast<uint32_t>(readByteFromBatch()) << (j * 8);
        }

        // Decode value 3
        output[3] = 0;
        for (int j = 0; j < len3; j++) {
            output[3] |= static_cast<uint32_t>(readByteFromBatch()) << (j * 8);
        }
    }
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
