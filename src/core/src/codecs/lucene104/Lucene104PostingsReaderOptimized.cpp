// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h"

#include "diagon/util/Exceptions.h"
#include "diagon/util/SearchProfiler.h"

#if defined(__AVX2__)
#    include <immintrin.h>
#elif defined(__SSE4_2__)
#    include <nmmintrin.h>
#endif

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104PostingsEnumOptimized::Lucene104PostingsEnumOptimized(
    std::unique_ptr<store::IndexInput> docIn, const TermState& termState, bool writeFreqs)
    : docIn_(std::move(docIn))
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

    // Get frequency from buffer
    currentFreq_ = writeFreqs_ ? static_cast<int32_t>(freqBuffer_[bufferPos_]) : 1;

    bufferPos_++;
    docsRead_++;
    return currentDoc_;
}

void Lucene104PostingsEnumOptimized::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

#if defined(__AVX2__)
    // AVX2 fast path: Decode 8 docs at a time using two interleaved 4-wide decodes
    // Writer format is interleaved: [4 doc deltas][4 freqs][4 doc deltas][4 freqs]
    // So we must decode 4 deltas, then 4 freqs, then 4 deltas, then 4 freqs
    constexpr int AVX2_GROUP_SIZE = 8;
    while (remaining >= AVX2_GROUP_SIZE && bufferIdx + AVX2_GROUP_SIZE <= BUFFER_SIZE) {
        // Prefetch next data for better cache performance
        __builtin_prefetch(&ioBatch_[ioBatchPos_ + 64], 0, 3);

        // First group: 4 doc deltas then 4 freqs
        decodeStreamVByte4(&docDeltaBuffer_[bufferIdx]);
        if (writeFreqs_) {
            decodeStreamVByte4(&freqBuffer_[bufferIdx]);
        }

        // Second group: 4 doc deltas then 4 freqs
        decodeStreamVByte4(&docDeltaBuffer_[bufferIdx + STREAMVBYTE_GROUP_SIZE]);
        if (writeFreqs_) {
            decodeStreamVByte4(&freqBuffer_[bufferIdx + STREAMVBYTE_GROUP_SIZE]);
        }

        if (!writeFreqs_) {
            // Set default frequencies using SIMD (faster than 8 scalar stores)
            __m256i ones = _mm256_set1_epi32(1);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&freqBuffer_[bufferIdx]), ones);
        }

        bufferIdx += AVX2_GROUP_SIZE;
        remaining -= AVX2_GROUP_SIZE;
    }
#endif

    // SSE/Scalar path: Decode 4 docs at a time
    while (remaining >= STREAMVBYTE_GROUP_SIZE &&
           bufferIdx + STREAMVBYTE_GROUP_SIZE <= BUFFER_SIZE) {
        // Decode doc deltas (stores deltas, not absolute docIDs)
        decodeStreamVByte4(&docDeltaBuffer_[bufferIdx]);

        // Decode frequencies
        if (writeFreqs_) {
            decodeStreamVByte4(&freqBuffer_[bufferIdx]);
        } else {
#if defined(__SSE4_2__) || defined(__AVX2__)
            // Set default frequencies using SIMD
            __m128i ones = _mm_set1_epi32(1);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&freqBuffer_[bufferIdx]), ones);
#else
            freqBuffer_[bufferIdx] = 1;
            freqBuffer_[bufferIdx + 1] = 1;
            freqBuffer_[bufferIdx + 2] = 1;
            freqBuffer_[bufferIdx + 3] = 1;
#endif
        }

        bufferIdx += STREAMVBYTE_GROUP_SIZE;
        remaining -= STREAMVBYTE_GROUP_SIZE;
    }

    // Handle remainder (< 4 docs) with VInt fallback
    int spaceLeft = BUFFER_SIZE - bufferIdx;
    int docsToRead = std::min(remaining, spaceLeft);

    for (int i = 0; i < docsToRead; ++i) {
        // Store delta (not absolute docID - accumulation happens in nextDoc())
        docDeltaBuffer_[bufferIdx + i] = static_cast<uint32_t>(readVIntFromBatch());

        if (writeFreqs_) {
            freqBuffer_[bufferIdx + i] = static_cast<uint32_t>(readVIntFromBatch());
        } else {
            freqBuffer_[bufferIdx + i] = 1;
        }
    }
    bufferIdx += docsToRead;

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
