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

// File extensions
static const std::string DOC_EXTENSION = "doc";
static const std::string SKIP_EXTENSION = "skp";

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
        return std::make_unique<Lucene104PostingsEnumOptimized>(docIn_->clone(), termState, writeFreqs);
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
    return std::make_unique<Lucene104PostingsEnumWithImpacts>(docIn_->clone(), termState, writeFreqs,
                                                               skipEntries);
}

void Lucene104PostingsReader::close() {
    if (docIn_) {
        docIn_.reset();
    }
    if (skipIn_) {
        skipIn_.reset();
    }
}

std::vector<SkipEntry> Lucene104PostingsReader::readSkipEntries(const TermState& termState) {
    std::vector<SkipEntry> entries;

    // Check if skip data exists
    static int debugCount = 0;
    if (debugCount++ < 10) {
        fprintf(stderr, "[DEBUG readSkipEntries] skipStartFP=%lld, skipIn_=%p, docFreq=%d\n",
                static_cast<long long>(termState.skipStartFP), (void*)skipIn_.get(), termState.docFreq);
    }

    if (termState.skipStartFP == -1 || !skipIn_) {
        if (debugCount <= 10) {
            fprintf(stderr, "[DEBUG readSkipEntries] Returning empty (skipStartFP=%lld, skipIn_=%p)\n",
                    static_cast<long long>(termState.skipStartFP), (void*)skipIn_.get());
        }
        return entries;  // No skip data (small postings list)
    }

    // Seek to start of skip data for this term
    skipIn_->seek(termState.skipStartFP);

    // Read number of skip entries
    int32_t numEntries = skipIn_->readVInt();
    entries.reserve(numEntries);

    if (debugCount <= 10) {
        fprintf(stderr, "[DEBUG readSkipEntries] Reading %d skip entries from FP %lld\n",
                numEntries, static_cast<long long>(termState.skipStartFP));
    }

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

        if (debugCount <= 10 && i < 3) {
            fprintf(stderr, "[DEBUG readSkipEntries] Entry %d: doc=%d, docFP=%lld, maxFreq=%d, maxNorm=%d\n",
                    i, entry.doc, static_cast<long long>(entry.docFP), entry.maxFreq, entry.maxNorm);
        }

        lastDoc = entry.doc;
        lastDocFP = entry.docFP;
    }

    if (debugCount <= 10) {
        fprintf(stderr, "[DEBUG readSkipEntries] Successfully read %zu skip entries\n", entries.size());
    }

    return entries;
}

// ==================== Lucene104PostingsEnumWithImpacts ====================

Lucene104PostingsEnumWithImpacts::Lucene104PostingsEnumWithImpacts(
    std::unique_ptr<store::IndexInput> docIn, const TermState& termState, bool writeFreqs,
    const std::vector<SkipEntry>& skipEntries)
    : docIn_(std::move(docIn))
    , docFreq_(termState.docFreq)
    , totalTermFreq_(termState.totalTermFreq)
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

void Lucene104PostingsEnumWithImpacts::advanceShallow(int target) {
    shallowTarget_ = target;

    // Update skip index to cover target
    while (currentSkipIndex_ < skipEntries_.size() &&
           skipEntries_[currentSkipIndex_].doc < target) {
        currentSkipIndex_++;
    }
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
        docsRead_ = (bestIdx + 1) * 128;  // Approximate docs read
        currentSkipIndex_ = bestIdx;
        return skipEntries_[bestIdx].docFP;
    }

    return -1;  // No suitable skip entry found
}

void Lucene104PostingsEnumWithImpacts::refillBuffer() {
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

int Lucene104PostingsEnumWithImpacts::getMaxFreq(int upTo) const {
    // Find the skip entry that covers the range [currentDoc_, upTo]
    // Return the maximum frequency from skip metadata
    if (skipEntries_.empty()) {
        // No skip data - return conservative maximum
        static int emptyCount = 0;
        if (emptyCount++ < 5) {
            fprintf(stderr, "[DEBUG getMaxFreq] No skip entries, returning max\n");
        }
        return std::numeric_limits<int>::max();
    }

    int maxFreq = 0;
    bool foundEntry = false;

    for (const auto& entry : skipEntries_) {
        if (entry.doc > upTo) {
            break;
        }
        if (entry.doc >= currentDoc_) {
            maxFreq = std::max(maxFreq, entry.maxFreq);
            foundEntry = true;
        }
    }

    // If no skip entries overlap [currentDoc, upTo], use first skip entry as conservative estimate
    // This handles the case where we're querying early document ranges before first skip entry
    if (!foundEntry && !skipEntries_.empty()) {
        // First skip entry represents max frequency for at least part of the postings list
        // Use it as upper bound for earlier ranges
        maxFreq = skipEntries_[0].maxFreq;
        foundEntry = true;
    }

    static int callCount = 0;
    if (callCount++ < 10) {
        fprintf(stderr, "[DEBUG getMaxFreq] upTo=%d, currentDoc=%d, skipEntries=%zu, maxFreq=%d (foundEntry=%d)\n",
                upTo, currentDoc_, skipEntries_.size(), maxFreq, foundEntry);
    }

    return maxFreq > 0 ? maxFreq : std::numeric_limits<int>::max();
}

int Lucene104PostingsEnumWithImpacts::getMaxNorm(int upTo) const {
    // Find the skip entry that covers the range [currentDoc_, upTo]
    // Return the maximum norm (shortest document = highest norm value)
    if (skipEntries_.empty()) {
        // No skip data - return shortest possible doc (127 = length 1.0)
        return 127;
    }

    int maxNorm = 0;
    bool foundEntry = false;

    for (const auto& entry : skipEntries_) {
        if (entry.doc > upTo) {
            break;
        }
        if (entry.doc >= currentDoc_) {
            maxNorm = std::max(maxNorm, static_cast<int>(entry.maxNorm));
            foundEntry = true;
        }
    }

    // If no skip entries overlap [currentDoc, upTo], use first skip entry as conservative estimate
    if (!foundEntry && !skipEntries_.empty()) {
        maxNorm = static_cast<int>(skipEntries_[0].maxNorm);
        foundEntry = true;
    }

    return maxNorm > 0 ? maxNorm : 127;
}

// ==================== Lucene104PostingsEnum ====================

Lucene104PostingsEnum::Lucene104PostingsEnum(std::unique_ptr<store::IndexInput> docIn, const TermState& termState,
                                             bool writeFreqs)
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
