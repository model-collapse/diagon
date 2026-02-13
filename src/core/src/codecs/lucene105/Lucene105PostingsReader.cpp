// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene105/Lucene105PostingsReader.h"

#include <algorithm>
#include <cstring>

namespace diagon {
namespace codecs {
namespace lucene105 {

static constexpr int32_t NO_MORE_DOCS = 0x7FFFFFFF;
static constexpr int ALIGNMENT = 64;

// ==================== Factory Methods ====================

std::unique_ptr<index::PostingsEnum> Lucene105PostingsReader::open(
    store::IndexInput* input,
    const TermState& termMeta) {

    return std::make_unique<Lucene105PostingsEnum>(input, termMeta);
}

std::unique_ptr<index::BatchPostingsEnum> Lucene105PostingsReader::openBatch(
    store::IndexInput* input,
    const TermState& termMeta) {

    return std::make_unique<Lucene105PostingsEnum>(input, termMeta);
}

// ==================== Lucene105PostingsEnum ====================

Lucene105PostingsEnum::Lucene105PostingsEnum(store::IndexInput* input,
                                             const TermState& termMeta)
    : input_(input)
    , termMeta_(termMeta)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0)
    , currentBlockIndex_(-1)
    , blockDocCount_(0)
    , bufferPos_(0) {

    // Seek to start of postings
    input_->seek(termMeta_.docStartFP);

    // Clear buffers
    std::memset(docBuffer_, 0, sizeof(docBuffer_));
    std::memset(freqBuffer_, 0, sizeof(freqBuffer_));
}

int Lucene105PostingsEnum::nextBatch(index::PostingsBatch& batch) {
    int count = 0;
    int remaining = termMeta_.docFreq - docsRead_;

    if (remaining <= 0) {
        batch.count = 0;
        return 0;
    }

    // Determine how many docs to return
    int toReturn = std::min(remaining, batch.capacity);

    while (count < toReturn) {
        // Read next block if buffer empty
        if (bufferPos_ >= blockDocCount_) {
            blockDocCount_ = readNextBlock();
            if (blockDocCount_ == 0) {
                break;  // No more blocks
            }
        }

        // How many docs can we take from current block?
        int available = blockDocCount_ - bufferPos_;
        int toTake = std::min(toReturn - count, available);

        // KEY OPTIMIZATION: Direct memcpy (no delta decoding!)
        // Doc IDs are already absolute, just copy
        std::memcpy(&batch.docs[count],
                   &docBuffer_[bufferPos_],
                   toTake * sizeof(int32_t));

        std::memcpy(&batch.freqs[count],
                   &freqBuffer_[bufferPos_],
                   toTake * sizeof(int32_t));

        bufferPos_ += toTake;
        count += toTake;
        docsRead_ += toTake;
    }

    // Update current position for one-at-a-time compatibility
    if (count > 0) {
        currentDoc_ = batch.docs[count - 1];
        currentFreq_ = batch.freqs[count - 1];
    }

    batch.count = count;
    return count;
}

int Lucene105PostingsEnum::readNextBlock() {
    if (currentBlockIndex_ + 1 >= termMeta_.numBlocks) {
        return 0;  // No more blocks
    }

    currentBlockIndex_++;
    bufferPos_ = 0;

    // Seek to cache-line aligned position
    seekToNextBlock();

    // Read block header (8 bytes)
    uint8_t blockSize = input_->readByte();
    uint8_t hasFreqs = input_->readByte();

    // Skip reserved bytes
    uint8_t reserved[6];
    input_->readBytes(reserved, 6);

    // Read doc IDs (64 bytes = 16 × 4)
    for (int i = 0; i < BLOCK_SIZE; i++) {
        docBuffer_[i] = input_->readInt();
    }

    // Read frequencies if present (64 bytes = 16 × 4)
    if (hasFreqs) {
        for (int i = 0; i < BLOCK_SIZE; i++) {
            freqBuffer_[i] = input_->readInt();
        }
    } else {
        // Fill with 1s if freqs not stored
        for (int i = 0; i < BLOCK_SIZE; i++) {
            freqBuffer_[i] = 1;
        }
    }

    return blockSize;
}

void Lucene105PostingsEnum::seekToNextBlock() {
    if (currentBlockIndex_ == 0) {
        // First block is at docStartFP (already seeked in constructor)
        return;
    }

    // Calculate aligned position
    // Each block: 8 (header) + 64 (docs) + 64 (freqs) = 136 bytes
    // Plus alignment padding to 64-byte boundary

    int64_t currentFP = input_->getFilePointer();
    int64_t remainder = currentFP % ALIGNMENT;

    if (remainder != 0) {
        // Skip padding to next cache line
        int64_t padding = ALIGNMENT - remainder;
        input_->seek(currentFP + padding);
    }
}

int Lucene105PostingsEnum::nextDoc() {
    if (docsRead_ >= termMeta_.docFreq) {
        currentDoc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    // Read next block if buffer empty
    if (bufferPos_ >= blockDocCount_) {
        blockDocCount_ = readNextBlock();
        if (blockDocCount_ == 0) {
            currentDoc_ = NO_MORE_DOCS;
            return NO_MORE_DOCS;
        }
    }

    // Get doc from buffer (already absolute ID, no delta decoding!)
    currentDoc_ = docBuffer_[bufferPos_];
    currentFreq_ = freqBuffer_[bufferPos_];

    bufferPos_++;
    docsRead_++;

    return currentDoc_;
}

int Lucene105PostingsEnum::advance(int target) {
    // Simple implementation: call nextDoc() until we reach target
    // TODO: Optimize with block skipping
    while (currentDoc_ < target) {
        if (nextDoc() == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }
    }
    return currentDoc_;
}

}  // namespace lucene105
}  // namespace codecs
}  // namespace diagon
