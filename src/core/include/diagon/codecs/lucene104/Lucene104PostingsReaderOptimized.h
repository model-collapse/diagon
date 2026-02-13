// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"
#include "diagon/util/StreamVByte.h"

#include <cstdint>
#include <iostream>
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
    Lucene104PostingsEnumOptimized(std::unique_ptr<store::IndexInput> docIn, const TermState& termState,
                                   bool writeFreqs);

    // ==================== DocIdSetIterator ====================

    int docID() const override { return currentDoc_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return docFreq_; }

    // ==================== PostingsEnum ====================

    int freq() const override { return currentFreq_; }

private:
    std::unique_ptr<store::IndexInput> docIn_;  // Owned clone
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
        // CRITICAL FIX: Preserve remaining bytes before refilling
        int remainingBytes = ioBatchLimit_ - ioBatchPos_;

        // Move remaining bytes to start of buffer
        if (remainingBytes > 0 && ioBatchPos_ > 0) {
            std::memmove(ioBatch_, ioBatch_ + ioBatchPos_, remainingBytes);
        }

        // Read more data to fill the rest of the buffer
        int64_t remaining = docIn_->length() - docIn_->getFilePointer();
        int spaceAvailable = IO_BATCH_SIZE - remainingBytes;
        int toRead = static_cast<int>(std::min(static_cast<int64_t>(spaceAvailable), remaining));

        if (toRead > 0) {
            docIn_->readBytes(ioBatch_ + remainingBytes, toRead);
            ioBatchPos_ = 0;
            ioBatchLimit_ = remainingBytes + toRead;
        } else {
            ioBatchPos_ = 0;
            ioBatchLimit_ = remainingBytes;
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
     * Read VInt from I/O batch buffer
     */
    inline int32_t readVIntFromBatch() {
        uint8_t b = readByteFromBatch();
        if ((b & 0x80) == 0) {
            return b;
        }
        int32_t i = b & 0x7F;
        b = readByteFromBatch();
        i |= (b & 0x7F) << 7;
        if ((b & 0x80) == 0) {
            return i;
        }
        b = readByteFromBatch();
        i |= (b & 0x7F) << 14;
        if ((b & 0x80) == 0) {
            return i;
        }
        b = readByteFromBatch();
        i |= (b & 0x7F) << 21;
        if ((b & 0x80) == 0) {
            return i;
        }
        b = readByteFromBatch();
        i |= (b & 0x0F) << 28;
        return i;
    }

    /**
     * SIMD StreamVByte decode for 4 integers
     * Uses AVX2/SSE/NEON when available, scalar fallback otherwise
     */
    inline void decodeStreamVByte4(uint32_t* output) {
        // Ensure we have enough bytes in batch buffer
        // Worst case: 1 control byte + 16 data bytes = 17 bytes
        if (ioBatchPos_ + 17 > ioBatchLimit_) {
            refillIOBatch();
        }

        // Use SIMD decode (AVX2/SSE/NEON)
        int bytesConsumed = util::StreamVByte::decode4(&ioBatch_[ioBatchPos_], output);
        ioBatchPos_ += bytesConsumed;
    }

#if defined(__AVX2__)
    /**
     * AVX2-optimized bulk decode for 8 integers
     * 2x throughput compared to decode4
     */
    inline void decodeStreamVByte8_AVX2(uint32_t* output) {
        // Ensure we have enough bytes for 2 groups
        // Worst case: 2 control bytes + 32 data bytes = 34 bytes
        if (ioBatchPos_ + 34 > ioBatchLimit_) {
            refillIOBatch();
        }

        // Use AVX2 8-wide decode (true 8-integer parallel decode)
        int bytesConsumed = util::StreamVByte::decode8_AVX2(&ioBatch_[ioBatchPos_], output);
        ioBatchPos_ += bytesConsumed;
    }
#endif
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
