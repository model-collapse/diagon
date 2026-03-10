// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"

#include "diagon/codecs/lucene104/Lucene104PostingsEnumBatch.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/util/BitPacking.h"
#include "diagon/util/Exceptions.h"

#include <cstring>
#include <iostream>

namespace diagon {
namespace codecs {
namespace lucene104 {

// File extensions
static const std::string DOC_EXTENSION = "doc";

// BitPack128 block size
static constexpr int BITPACK_BLOCK_SIZE = 128;

// Helper: unpack freq from low bit of doc deltas after BitPack decode.
// Reads non-1 frequencies as VInts from the IndexInput stream.
static inline void unpackFreqFromDocDeltas(uint32_t* docDeltaBuf, uint32_t* freqBuf, int offset,
                                           int count, store::IndexInput* docIn) {
    for (int i = 0; i < count; i++) {
        if (docDeltaBuf[offset + i] & 1) {
            freqBuf[offset + i] = 1;
        } else {
            freqBuf[offset + i] = static_cast<uint32_t>(docIn->readVInt());
        }
        docDeltaBuf[offset + i] >>= 1;
    }
}

// Helper: read a PFOR-Delta block from IndexInput into docDeltaBuffer at given offset.
// Token format: (numExceptions << 5) | patchedBitsRequired
static inline void readBitPackBlock(store::IndexInput* docIn, uint32_t* docDeltaBuf,
                                    uint32_t* freqBuf, int offset, int blockSize, bool writeFreqs) {
    // Read token byte: (numExceptions << 5) | bitsPerValue
    uint8_t token = docIn->readByte();
    int bpv = token & 0x1F;
    int numEx = token >> 5;

    // Build encoded buffer for decode()
    uint8_t encoded[util::BitPacking::maxBytesPerBlock(BITPACK_BLOCK_SIZE)];
    encoded[0] = token;
    int encodedPos = 1;

    if (bpv == 0 && numEx == 0) {
        // All-equal case: read VInt bytes
        while (true) {
            uint8_t b = docIn->readByte();
            encoded[encodedPos++] = b;
            if ((b & 0x80) == 0)
                break;
        }
    } else {
        // Read packed data + exception pairs
        int dataBytes = (bpv == 0) ? 0 : (blockSize * bpv + 7) / 8;
        int exBytes = numEx * 2;
        if (dataBytes + exBytes > 0) {
            docIn->readBytes(encoded + 1, dataBytes + exBytes);
        }
    }

    // Decode PFOR block
    util::BitPacking::decode(encoded, blockSize, &docDeltaBuf[offset]);

    // Unpack freq from low bit; read non-1 freqs as VInts
    if (writeFreqs) {
        unpackFreqFromDocDeltas(docDeltaBuf, freqBuf, offset, blockSize, docIn);
    }
}

Lucene104PostingsReader::Lucene104PostingsReader(index::SegmentReadState& state)
    : docIn_(nullptr)
    , skipIn_(nullptr)
    , segmentName_(state.segmentName)
    , segmentSuffix_(state.segmentSuffix) {
    // Create .doc input file
    std::string docFileName = segmentName_;
    if (!segmentSuffix_.empty()) {
        docFileName += "_" + segmentSuffix_;
    }
    docFileName += "." + DOC_EXTENSION;

    // For Phase 2 MVP, docIn_ and skipIn_ will be set externally
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
    // Clone IndexInput for thread-safe independent reading
    return std::make_unique<Lucene104PostingsEnumOptimized>(docIn_->clone(), termState, writeFreqs);
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
        return std::make_unique<Lucene104PostingsEnumBatch>(docIn_->clone(), termState, writeFreqs);
    } else {
        // Return regular optimized version
        return std::make_unique<Lucene104PostingsEnumOptimized>(docIn_->clone(), termState,
                                                                writeFreqs);
    }
}

std::unique_ptr<index::PostingsEnum>
Lucene104PostingsReader::impactsPostings(const index::FieldInfo& fieldInfo,
                                         const TermState& termState) {
    if (!docIn_) {
        throw std::runtime_error("No input set for PostingsReader");
    }

    // Determine if frequencies are written
    bool writeFreqs = (fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS);

    // Read skip entries if available
    std::vector<SkipEntry> skipEntries = readSkipEntries(termState);

    // Return impacts-aware enum
    // Clone IndexInput for thread-safe independent reading
    return std::make_unique<Lucene104PostingsEnumWithImpacts>(docIn_->clone(), termState,
                                                              writeFreqs, skipEntries);
}

std::unique_ptr<index::PostingsEnum>
Lucene104PostingsReader::postingsWithPositions(const index::FieldInfo& fieldInfo,
                                               const TermState& termState) {
    if (!docIn_) {
        throw std::runtime_error("No input set for PostingsReader");
    }

    if (termState.posStartFP < 0 || !posIn_) {
        // No position data - fall back to regular postings
        return postings(fieldInfo, termState);
    }

    bool writeFreqs = (fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS);

    return std::make_unique<Lucene104PostingsEnumWithPositions>(docIn_->clone(), posIn_->clone(),
                                                                termState, writeFreqs);
}

void Lucene104PostingsReader::close() {
    if (docIn_) {
        docIn_.reset();
    }
    if (skipIn_) {
        skipIn_.reset();
    }
    if (posIn_) {
        posIn_.reset();
    }
}

std::vector<SkipEntry> Lucene104PostingsReader::readSkipEntries(const TermState& termState) {
    std::vector<SkipEntry> entries;

    // Check if skip data exists
    if (termState.skipStartFP == -1 || !skipIn_) {
        return entries;  // No skip data (small postings list)
    }

    // Seek to start of skip data for this term
    skipIn_->seek(termState.skipStartFP);

    // Read number of skip entries
    int32_t numEntries = skipIn_->readVInt();
    entries.reserve(numEntries);

    // Read skip entries with delta decoding
    int32_t lastDoc = 0;
    int64_t lastDocFP = termState.docStartFP;

    for (int i = 0; i < numEntries; ++i) {
        SkipEntry entry;

        // Delta decode doc ID
        int32_t docDelta = skipIn_->readVInt();
        entry.doc = lastDoc + docDelta;

        // Delta decode file pointer
        int64_t docFPDelta = skipIn_->readVLong();
        entry.docFP = lastDocFP + docFPDelta;

        // Read impact metadata
        entry.maxFreq = skipIn_->readVInt();
        entry.maxNorm = static_cast<int8_t>(skipIn_->readByte());

        entries.push_back(entry);

        lastDoc = entry.doc;
        lastDocFP = entry.docFP;
    }

    return entries;
}

// ==================== Lucene104PostingsEnumWithImpacts ====================

Lucene104PostingsEnumWithImpacts::Lucene104PostingsEnumWithImpacts(
    std::unique_ptr<store::IndexInput> docIn, const TermState& termState, bool writeFreqs,
    const std::vector<SkipEntry>& skipEntries)
    : docIn_(std::move(docIn))
    , mmapInput_(dynamic_cast<store::MMapIndexInput*>(docIn_.get()))
    , docFreq_(termState.docFreq)
    , writeFreqs_(writeFreqs)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0)
    , skipEntries_(skipEntries)
    , currentSkipIndex_(0)
    , shallowTarget_(-1)
    , docDeltaBuffer_{}  // Zero-initialize
    , freqBuffer_{}      // Zero-initialize
    , bufferPos_(0)
    , bufferLimit_(0) {
    // Seek to start of this term's postings
    docIn_->seek(termState.docStartFP);
}

int Lucene104PostingsEnumWithImpacts::nextDoc() {
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

int Lucene104PostingsEnumWithImpacts::advance(int target) {
    if (target <= currentDoc_) {
        return currentDoc_;
    }

    // Use skip list if available and beneficial
    if (!skipEntries_.empty() && target > currentDoc_ + 128) {
        int64_t skipFP = skipToTarget(target);
        if (skipFP >= 0) {
            docIn_->seek(skipFP);
            bufferPos_ = 0;
            bufferLimit_ = 0;
        }
    }

    // Linear scan to target
    while (currentDoc_ < target) {
        if (nextDoc() == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }
    }
    return currentDoc_;
}

int Lucene104PostingsEnumWithImpacts::advanceShallow(int target) {
    shallowTarget_ = target;

    // Update skip index to cover target
    while (currentSkipIndex_ < static_cast<int>(skipEntries_.size()) &&
           skipEntries_[currentSkipIndex_].doc < target) {
        currentSkipIndex_++;
    }

    // Return the block boundary (last doc in current block)
    if (currentSkipIndex_ < static_cast<int>(skipEntries_.size())) {
        return skipEntries_[currentSkipIndex_].doc;
    }
    return NO_MORE_DOCS;
}

float Lucene104PostingsEnumWithImpacts::getMaxScore(int upTo, float k1, float b,
                                                    float avgFieldLength) const {
    if (skipEntries_.empty()) {
        // No skip data, use term-level max (conservative)
        // This would require storing term-level max_freq/max_norm
        // For now, return a large value to disable pruning
        return 1e9f;
    }

    // Find skip entries that cover range [currentDoc, upTo]
    float maxScore = 0.0f;

    for (size_t i = currentSkipIndex_; i < skipEntries_.size(); ++i) {
        const auto& entry = skipEntries_[i];

        // Stop if beyond upTo
        if (entry.doc > upTo) {
            break;
        }

        // Compute max possible BM25 score for this block
        // BM25 = IDF * (freq * (k1 + 1)) / (freq + k1 * (1 - b + b * fieldLength / avgFieldLength))
        // For upper bound: use maxFreq and minNorm (norm=0 means longest doc)
        int maxFreq = entry.maxFreq;
        int maxNorm = entry.maxNorm;  // Larger norm = shorter doc = higher score

        // Simplified BM25 upper bound (Lucene 8+ formula, assuming IDF=1)
        // TODO: Pass actual IDF from scorer
        float score = static_cast<float>(maxFreq) /
                      (maxFreq + k1 * (1 - b + b * (1.0f / (maxNorm + 1))));

        maxScore = std::max(maxScore, score);
    }

    return maxScore;
}

int64_t Lucene104PostingsEnumWithImpacts::skipToTarget(int target) {
    // Binary search for skip entry before target
    int left = 0;
    int right = static_cast<int>(skipEntries_.size()) - 1;
    int bestIdx = -1;

    while (left <= right) {
        int mid = (left + right) / 2;
        if (skipEntries_[mid].doc < target) {
            bestIdx = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    if (bestIdx >= 0) {
        currentDoc_ = skipEntries_[bestIdx].doc - 1;  // Will advance to this doc
        docsRead_ = (bestIdx + 1) * 128;              // Approximate docs read
        currentSkipIndex_ = bestIdx;
        return skipEntries_[bestIdx].docFP;
    }

    return -1;  // No suitable skip entry found
}

void Lucene104PostingsEnumWithImpacts::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

    // Read one BitPack128 block if we have >= 128 remaining docs
    if (remaining >= BITPACK_BLOCK_SIZE) {
        readBitPackBlock(docIn_.get(), docDeltaBuffer_, freqBuffer_, bufferIdx, BITPACK_BLOCK_SIZE,
                         writeFreqs_);
        bufferIdx += BITPACK_BLOCK_SIZE;
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

int Lucene104PostingsEnumWithImpacts::drainBatch(int upTo, int* outDocs, int* outFreqs,
                                                 int maxCount) {
    int count = 0;

    while (count < maxCount && currentDoc_ < upTo && currentDoc_ != NO_MORE_DOCS) {
        // Output current doc (already positioned by prior nextDoc/advance/drainBatch)
        outDocs[count] = currentDoc_;
        outFreqs[count] = currentFreq_;
        count++;

        // Inline nextDoc logic (no virtual dispatch)
        if (docsRead_ >= docFreq_) {
            currentDoc_ = NO_MORE_DOCS;
            break;
        }

        if (bufferPos_ >= bufferLimit_) {
            refillBuffer();
            if (bufferLimit_ == 0) {
                currentDoc_ = NO_MORE_DOCS;
                break;
            }
        }

        int docDelta = static_cast<int>(docDeltaBuffer_[bufferPos_]);
        currentDoc_ += docDelta;
        currentFreq_ = writeFreqs_ ? static_cast<int>(freqBuffer_[bufferPos_]) : 1;
        bufferPos_++;
        docsRead_++;
    }

    return count;
}

int Lucene104PostingsEnumWithImpacts::getMaxFreq(int upTo) const {
    if (skipEntries_.empty()) {
        return std::numeric_limits<int>::max();
    }

    int maxFreq = 0;
    bool foundEntry = false;

    // Start from currentSkipIndex_ (maintained by advanceShallow) to skip already-passed entries
    size_t startIdx = (currentSkipIndex_ > 0) ? static_cast<size_t>(currentSkipIndex_ - 1) : 0;
    for (size_t i = startIdx; i < skipEntries_.size(); ++i) {
        const auto& entry = skipEntries_[i];
        if (entry.doc > upTo) {
            break;
        }
        if (entry.doc >= currentDoc_) {
            maxFreq = std::max(maxFreq, entry.maxFreq);
            foundEntry = true;
        }
    }

    if (!foundEntry && !skipEntries_.empty()) {
        maxFreq = skipEntries_[0].maxFreq;
        foundEntry = true;
    }

    return maxFreq > 0 ? maxFreq : std::numeric_limits<int>::max();
}

int Lucene104PostingsEnumWithImpacts::getMaxNorm(int upTo) const {
    if (skipEntries_.empty()) {
        return 127;
    }

    int maxNorm = 0;
    bool foundEntry = false;

    // Start from currentSkipIndex_ to skip already-passed entries
    size_t startIdx = (currentSkipIndex_ > 0) ? static_cast<size_t>(currentSkipIndex_ - 1) : 0;
    for (size_t i = startIdx; i < skipEntries_.size(); ++i) {
        const auto& entry = skipEntries_[i];
        if (entry.doc > upTo) {
            break;
        }
        if (entry.doc >= currentDoc_) {
            maxNorm = std::max(maxNorm, static_cast<int>(entry.maxNorm));
            foundEntry = true;
        }
    }

    if (!foundEntry && !skipEntries_.empty()) {
        maxNorm = static_cast<int>(skipEntries_[0].maxNorm);
        foundEntry = true;
    }

    return maxNorm > 0 ? maxNorm : 127;
}

void Lucene104PostingsEnumWithImpacts::getMaxFreqAndNorm(int upTo, int& outMaxFreq,
                                                         int& outMaxNorm) const {
    if (skipEntries_.empty()) {
        outMaxFreq = std::numeric_limits<int>::max();
        outMaxNorm = 127;
        return;
    }

    int maxFreq = 0;
    int maxNorm = 0;
    bool foundEntry = false;

    size_t startIdx = (currentSkipIndex_ > 0) ? static_cast<size_t>(currentSkipIndex_ - 1) : 0;
    for (size_t i = startIdx; i < skipEntries_.size(); ++i) {
        const auto& entry = skipEntries_[i];
        if (entry.doc > upTo) {
            break;
        }
        if (entry.doc >= currentDoc_) {
            maxFreq = std::max(maxFreq, entry.maxFreq);
            maxNorm = std::max(maxNorm, static_cast<int>(entry.maxNorm));
            foundEntry = true;
        }
    }

    if (!foundEntry && !skipEntries_.empty()) {
        maxFreq = skipEntries_[0].maxFreq;
        maxNorm = static_cast<int>(skipEntries_[0].maxNorm);
    }

    outMaxFreq = maxFreq > 0 ? maxFreq : std::numeric_limits<int>::max();
    outMaxNorm = maxNorm > 0 ? maxNorm : 127;
}

int Lucene104PostingsEnumWithImpacts::getNextBlockBoundary(int target) const {
    // Phase 2: Smart upTo calculation
    // Find the next block boundary from skip entries
    if (skipEntries_.empty()) {
        // No skip data - fall back to fixed 128-doc window
        return (target < NO_MORE_DOCS - 128) ? target + 128 : NO_MORE_DOCS;
    }

    // Binary search for first skip entry at or after target
    for (const auto& entry : skipEntries_) {
        if (entry.doc >= target) {
            return entry.doc;
        }
    }

    // No more skip entries - return NO_MORE_DOCS
    return NO_MORE_DOCS;
}

// ==================== Lucene104PostingsEnum ====================

Lucene104PostingsEnum::Lucene104PostingsEnum(std::unique_ptr<store::IndexInput> docIn,
                                             const TermState& termState, bool writeFreqs)
    : docIn_(std::move(docIn))
    , docFreq_(termState.docFreq)
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

    // Read one BitPack128 block if we have >= 128 remaining docs
    if (remaining >= BITPACK_BLOCK_SIZE) {
        readBitPackBlock(docIn_.get(), docDeltaBuffer_, freqBuffer_, bufferIdx, BITPACK_BLOCK_SIZE,
                         writeFreqs_);
        bufferIdx += BITPACK_BLOCK_SIZE;
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

// ==================== Lucene104PostingsEnumWithPositions ====================

Lucene104PostingsEnumWithPositions::Lucene104PostingsEnumWithPositions(
    std::unique_ptr<store::IndexInput> docIn, std::unique_ptr<store::IndexInput> posIn,
    const TermState& termState, bool writeFreqs)
    : docIn_(std::move(docIn))
    , posIn_(std::move(posIn))
    , docFreq_(termState.docFreq)
    , writeFreqs_(writeFreqs)
    , currentDoc_(-1)
    , currentFreq_(1)
    , docsRead_(0)
    , positionsRemaining_(0)
    , lastPosition_(0)
    , docDeltaBuffer_{}
    , freqBuffer_{}
    , bufferPos_(0)
    , bufferLimit_(0)
    , posBuffer_{}
    , posBufPos_(0)
    , posBufLimit_(0)
    , totalPosRead_(0)
    , totalPosCount_(termState.totalTermFreq) {
    // Seek to start of this term's doc/freq data
    docIn_->seek(termState.docStartFP);
    // Seek to start of this term's position data
    posIn_->seek(termState.posStartFP);
}

int Lucene104PostingsEnumWithPositions::nextDoc() {
    // Skip any unread positions from previous doc
    skipPositions();

    if (docsRead_ >= docFreq_) {
        currentDoc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    // Refill doc/freq buffer if empty
    if (bufferPos_ >= bufferLimit_) {
        refillBuffer();
    }

    // Get doc delta from buffer
    int docDelta = static_cast<int32_t>(docDeltaBuffer_[bufferPos_]);

    // Update current doc (delta encoding)
    if (currentDoc_ == -1) {
        currentDoc_ = docDelta;
    } else {
        currentDoc_ += docDelta;
    }

    // Get frequency from buffer
    currentFreq_ = writeFreqs_ ? static_cast<int32_t>(freqBuffer_[bufferPos_]) : 1;

    bufferPos_++;
    docsRead_++;

    // Reset position state for new doc
    positionsRemaining_ = currentFreq_;
    lastPosition_ = 0;

    return currentDoc_;
}

int Lucene104PostingsEnumWithPositions::advance(int target) {
    while (currentDoc_ < target) {
        if (nextDoc() == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }
    }
    return currentDoc_;
}

int Lucene104PostingsEnumWithPositions::nextPosition() {
    if (positionsRemaining_ <= 0) {
        return -1;
    }

    // Refill position buffer if empty
    if (posBufPos_ >= posBufLimit_) {
        refillPosBuffer();
    }

    // Delta decode position from buffer
    int posDelta = static_cast<int>(posBuffer_[posBufPos_++]);
    lastPosition_ += posDelta;
    positionsRemaining_--;
    totalPosRead_++;
    return lastPosition_;
}

void Lucene104PostingsEnumWithPositions::skipPositions() {
    // Skip any unread positions from previous doc
    while (positionsRemaining_ > 0) {
        if (posBufPos_ >= posBufLimit_) {
            refillPosBuffer();
        }
        posBufPos_++;
        positionsRemaining_--;
        totalPosRead_++;
    }
}

void Lucene104PostingsEnumWithPositions::refillPosBuffer() {
    posBufPos_ = 0;
    int64_t remaining = totalPosCount_ - totalPosRead_;
    int bufferIdx = 0;

    // Read one PFOR-Delta block if we have >= 128 remaining positions
    if (remaining >= BITPACK_BLOCK_SIZE) {
        // Read token byte: (numExceptions << 5) | bitsPerValue
        uint8_t token = posIn_->readByte();
        int bpv = token & 0x1F;
        int numEx = token >> 5;

        uint8_t encoded[util::BitPacking::maxBytesPerBlock(BITPACK_BLOCK_SIZE)];
        encoded[0] = token;

        if (bpv == 0 && numEx == 0) {
            // All-equal case: read VInt bytes
            int encodedPos = 1;
            while (true) {
                uint8_t b = posIn_->readByte();
                encoded[encodedPos++] = b;
                if ((b & 0x80) == 0)
                    break;
            }
        } else {
            int dataBytes = (bpv == 0) ? 0 : (BITPACK_BLOCK_SIZE * bpv + 7) / 8;
            int exBytes = numEx * 2;
            if (dataBytes + exBytes > 0) {
                posIn_->readBytes(encoded + 1, dataBytes + exBytes);
            }
        }

        util::BitPacking::decode(encoded, BITPACK_BLOCK_SIZE, posBuffer_);
        bufferIdx = BITPACK_BLOCK_SIZE;
    } else if (remaining > 0) {
        // VInt tail for remaining < 128 positions
        int toRead = static_cast<int>(remaining);
        for (int i = 0; i < toRead; ++i) {
            posBuffer_[i] = static_cast<uint32_t>(posIn_->readVInt());
        }
        bufferIdx = toRead;
    }

    posBufLimit_ = bufferIdx;
}

void Lucene104PostingsEnumWithPositions::refillBuffer() {
    bufferPos_ = 0;
    int remaining = docFreq_ - docsRead_;
    int bufferIdx = 0;

    // Read one BitPack128 block if we have >= 128 remaining docs
    if (remaining >= BITPACK_BLOCK_SIZE) {
        readBitPackBlock(docIn_.get(), docDeltaBuffer_, freqBuffer_, bufferIdx, BITPACK_BLOCK_SIZE,
                         writeFreqs_);
        bufferIdx += BITPACK_BLOCK_SIZE;
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

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
