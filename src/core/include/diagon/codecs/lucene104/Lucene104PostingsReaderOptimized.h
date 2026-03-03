// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"
#include "diagon/util/BitPacking.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Optimized PostingsEnum implementation for Lucene104 format.
 *
 * Uses BitPack128 encoding: 128-doc blocks with minimum bits-per-value.
 * I/O batch buffer reduces virtual call overhead for VInt reads.
 */
class Lucene104PostingsEnumOptimized : public index::PostingsEnum {
public:
    Lucene104PostingsEnumOptimized(std::unique_ptr<store::IndexInput> docIn,
                                   const TermState& termState, bool writeFreqs);

    int docID() const override { return currentDoc_; }
    int nextDoc() override;
    int advance(int target) override;
    int64_t cost() const override { return docFreq_; }
    int freq() const override { return currentFreq_; }

private:
    std::unique_ptr<store::IndexInput> docIn_;
    int docFreq_;
    bool writeFreqs_;

    int currentDoc_;
    int currentFreq_;
    int docsRead_;

    // BitPack128 buffering: 128 docs per block
    static constexpr int BUFFER_SIZE = 128;
    uint32_t docDeltaBuffer_[BUFFER_SIZE];
    uint32_t freqBuffer_[BUFFER_SIZE];
    int bufferPos_;
    int bufferLimit_;

    // I/O batch buffer for VInt reads (non-1 freqs in blocks and VInt tail)
    static constexpr int IO_BATCH_SIZE = 1024;
    uint8_t ioBatch_[IO_BATCH_SIZE];
    int ioBatchPos_;
    int ioBatchLimit_;

    void refillBuffer();

    inline void refillIOBatch() {
        int remainingBytes = ioBatchLimit_ - ioBatchPos_;
        if (remainingBytes > 0 && ioBatchPos_ > 0) {
            std::memmove(ioBatch_, ioBatch_ + ioBatchPos_, remainingBytes);
        }
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

    inline uint8_t readByteFromBatch() {
        if (ioBatchPos_ >= ioBatchLimit_) {
            refillIOBatch();
        }
        return ioBatch_[ioBatchPos_++];
    }

    inline void readBytesFromBatch(uint8_t* dest, int count) {
        while (count > 0) {
            if (ioBatchPos_ >= ioBatchLimit_) {
                refillIOBatch();
            }
            int avail = ioBatchLimit_ - ioBatchPos_;
            int toCopy = std::min(avail, count);
            std::memcpy(dest, ioBatch_ + ioBatchPos_, toCopy);
            ioBatchPos_ += toCopy;
            dest += toCopy;
            count -= toCopy;
        }
    }

    inline int32_t readVIntFromBatch() {
        uint8_t b = readByteFromBatch();
        if ((b & 0x80) == 0)
            return b;
        int32_t i = b & 0x7F;
        b = readByteFromBatch();
        i |= (b & 0x7F) << 7;
        if ((b & 0x80) == 0)
            return i;
        b = readByteFromBatch();
        i |= (b & 0x7F) << 14;
        if ((b & 0x80) == 0)
            return i;
        b = readByteFromBatch();
        i |= (b & 0x7F) << 21;
        if ((b & 0x80) == 0)
            return i;
        b = readByteFromBatch();
        i |= (b & 0x0F) << 28;
        return i;
    }
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
