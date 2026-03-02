// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"

#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/BitPacking.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// File extensions
static const std::string DOC_EXTENSION = "doc";
static const std::string SKIP_EXTENSION = "skp";
static const std::string POS_EXTENSION = "pos";

Lucene104PostingsWriter::Lucene104PostingsWriter(index::SegmentWriteState& state)
    : docOut_(nullptr)
    , skipOut_(nullptr)
    , posOut_(nullptr)
    , indexOptions_(index::IndexOptions::DOCS)
    , writeFreqs_(false)
    , writePositions_(false)
    , docStartFP_(0)
    , skipStartFP_(-1)
    , lastDocID_(0)
    , docCount_(0)
    , totalTermFreq_(0)
    , directory_(state.directory)
    , segmentName_(state.segmentName)
    , segmentSuffix_(state.segmentSuffix)
    , posStartFP_(-1)
    , lastPosition_(0)
    , docDeltaBuffer_{}  // Zero-initialize
    , freqBuffer_{}      // Zero-initialize
    , bufferPos_(0)
    , posDeltaBuffer_{}  // Zero-initialize
    , posBufferPos_(0)
    , blockMaxFreq_(0)
    , blockMaxNorm_(0)
    , docsSinceLastSkip_(0)
    , lastSkipDocFP_(0)
    , lastSkipDoc_(0) {
    // Create .doc output file name
    docFileName_ = segmentName_;
    if (!segmentSuffix_.empty()) {
        docFileName_ += "_" + segmentSuffix_;
    }
    docFileName_ += "." + DOC_EXTENSION;

    // Create .skp output file name (for skip entries with impacts)
    skipFileName_ = segmentName_;
    if (!segmentSuffix_.empty()) {
        skipFileName_ += "_" + segmentSuffix_;
    }
    skipFileName_ += "." + SKIP_EXTENSION;

    // Create .pos output file name (for positions)
    posFileName_ = segmentName_;
    if (!segmentSuffix_.empty()) {
        posFileName_ += "_" + segmentSuffix_;
    }
    posFileName_ += "." + POS_EXTENSION;

    // Use in-memory buffers - FieldsConsumer will write to actual files
    docOut_ = std::make_unique<store::ByteBuffersIndexOutput>(docFileName_);
    skipOut_ = std::make_unique<store::ByteBuffersIndexOutput>(skipFileName_);
    posOut_ = std::make_unique<store::ByteBuffersIndexOutput>(posFileName_);
}

Lucene104PostingsWriter::~Lucene104PostingsWriter() {
    // Ensure close() was called
    if (docOut_ || skipOut_ || posOut_) {
        try {
            close();
        } catch (...) {
            // Swallow exceptions in destructor
        }
    }
}

void Lucene104PostingsWriter::setField(const index::FieldInfo& fieldInfo) {
    indexOptions_ = fieldInfo.indexOptions;

    // Determine what to write based on index options
    writeFreqs_ = (indexOptions_ >= index::IndexOptions::DOCS_AND_FREQS);
    writePositions_ = (indexOptions_ >= index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
}

void Lucene104PostingsWriter::startTerm() {
    // Record file pointer at start of this term's postings
    docStartFP_ = docOut_->getFilePointer();
    skipStartFP_ = -1;  // Will be set when first skip entry is written
    lastDocID_ = 0;
    docCount_ = 0;
    totalTermFreq_ = 0;
    bufferPos_ = 0;   // Reset doc buffer
    posBufferPos_ = 0;  // Reset position buffer

    // Record position file pointer for this term
    posStartFP_ = writePositions_ ? posOut_->getFilePointer() : -1;
    lastPosition_ = 0;

    // Reset block-level tracking for impacts
    blockMaxFreq_ = 0;
    blockMaxNorm_ = 0;
    docsSinceLastSkip_ = 0;
    lastSkipDocFP_ = docStartFP_;
    lastSkipDoc_ = 0;

    // Clear skip entries from previous term
    skipEntries_.clear();
}

void Lucene104PostingsWriter::startDoc(int docID, int freq, int8_t norm) {
    if (docID < 0) {
        throw std::invalid_argument("docID must be >= 0");
    }

    if (docCount_ > 0 && docID <= lastDocID_) {
        throw std::invalid_argument("docs must be added in order (docID " + std::to_string(docID) +
                                    " <= lastDocID " + std::to_string(lastDocID_) + ")");
    }

    if (freq <= 0) {
        throw std::invalid_argument("freq must be > 0");
    }

    // Track max frequency and norm for current block (for Block-Max WAND)
    blockMaxFreq_ = std::max(blockMaxFreq_, freq);
    blockMaxNorm_ = std::max(blockMaxNorm_, norm);
    docsSinceLastSkip_++;

    // Check if we need to create a skip entry
    maybeFlushSkipEntry();

    // Buffer doc delta and frequency for StreamVByte encoding
    int docDelta = docID - lastDocID_;

    docDeltaBuffer_[bufferPos_] = static_cast<uint32_t>(docDelta);
    freqBuffer_[bufferPos_] = static_cast<uint32_t>(freq);
    bufferPos_++;

    // Update state
    if (writeFreqs_) {
        totalTermFreq_ += freq;
    } else {
        totalTermFreq_ = -1;  // Not tracked for DOCS_ONLY
    }

    lastDocID_ = docID;
    docCount_++;

    // Reset position delta for new document
    lastPosition_ = 0;

    // Flush when buffer is full
    if (bufferPos_ >= BUFFER_SIZE) {
        flushBuffer();
    }
}

void Lucene104PostingsWriter::addPosition(int position) {
    if (!writePositions_)
        return;

    // Delta-encode positions within a document
    int posDelta = position - lastPosition_;
    posDeltaBuffer_[posBufferPos_++] = static_cast<uint32_t>(posDelta);
    lastPosition_ = position;

    // Flush when position buffer is full (128 positions)
    if (posBufferPos_ >= BUFFER_SIZE) {
        flushPositionBuffer();
    }
}

TermState Lucene104PostingsWriter::finishTerm() {
    // Flush any remaining buffered doc data (VInt tail for < 128 docs)
    if (bufferPos_ > 0) {
        for (int i = 0; i < bufferPos_; ++i) {
            if (writeFreqs_) {
                // Pack freq=1 into low bit of doc delta (same encoding as BitPack blocks)
                if (freqBuffer_[i] == 1) {
                    docOut_->writeVInt(static_cast<int32_t>((docDeltaBuffer_[i] << 1) | 1));
                } else {
                    docOut_->writeVInt(static_cast<int32_t>(docDeltaBuffer_[i] << 1));
                    docOut_->writeVInt(static_cast<int32_t>(freqBuffer_[i]));
                }
            } else {
                docOut_->writeVInt(static_cast<int32_t>(docDeltaBuffer_[i]));
            }
        }
        bufferPos_ = 0;
    }

    // Flush any remaining buffered position data (VInt tail for < 128 positions)
    if (posBufferPos_ > 0) {
        for (int i = 0; i < posBufferPos_; ++i) {
            posOut_->writeVInt(static_cast<int32_t>(posDeltaBuffer_[i]));
        }
        posBufferPos_ = 0;
    }

    // Flush final skip entry if there are remaining docs
    if (docsSinceLastSkip_ > 0 && !skipEntries_.empty()) {
        // Only create final skip entry if we have a previous one
        // (no point in single skip entry for small postings lists)
        SkipEntry entry;
        entry.doc = lastDocID_;
        entry.docFP = docOut_->getFilePointer();
        entry.maxFreq = blockMaxFreq_;
        entry.maxNorm = blockMaxNorm_;
        skipEntries_.push_back(entry);
    }

    // Write skip data to .skp file
    writeSkipData();

    TermState state;
    state.docStartFP = docStartFP_;
    state.posStartFP = posStartFP_;
    state.skipStartFP = skipStartFP_;
    state.docFreq = docCount_;
    state.totalTermFreq = totalTermFreq_;
    state.skipEntryCount = static_cast<int>(skipEntries_.size());

    return state;
}

void Lucene104PostingsWriter::flushBuffer() {
    if (bufferPos_ != BUFFER_SIZE) {
        return;  // Only flush when buffer is full (128 docs)
    }

    // Pack freq=1 into low bit of doc delta (Lucene-compatible encoding).
    // For freq==1: docDelta = (docDelta << 1) | 1  (no separate freq needed)
    // For freq>1:  docDelta = (docDelta << 1) | 0  (freq written as VInt after block)
    // ~60-70% of postings have freq=1, so this eliminates most freq bytes.
    if (writeFreqs_) {
        for (int i = 0; i < BUFFER_SIZE; i++) {
            if (freqBuffer_[i] == 1) {
                docDeltaBuffer_[i] = (docDeltaBuffer_[i] << 1) | 1;
            } else {
                docDeltaBuffer_[i] = (docDeltaBuffer_[i] << 1);
            }
        }
    }

    // Encode 128 doc deltas using BitPacking (includes freq=1 info in low bit)
    uint8_t encoded[util::BitPacking::maxBytesPerBlock(BUFFER_SIZE)];
    int encodedBytes = util::BitPacking::encode(docDeltaBuffer_, BUFFER_SIZE, encoded);
    docOut_->writeBytes(encoded, encodedBytes);

    // Write only non-1 frequencies as VInts (freq=1 is already encoded in doc delta low bit)
    if (writeFreqs_) {
        for (int i = 0; i < BUFFER_SIZE; i++) {
            if (freqBuffer_[i] != 1) {
                docOut_->writeVInt(static_cast<int32_t>(freqBuffer_[i]));
            }
        }
    }

    bufferPos_ = 0;  // Reset buffer
}

void Lucene104PostingsWriter::flushPositionBuffer() {
    if (posBufferPos_ != BUFFER_SIZE) {
        return;  // Only flush when buffer is full (128 positions)
    }

    // Encode 128 position deltas using BitPacking
    uint8_t encoded[util::BitPacking::maxBytesPerBlock(BUFFER_SIZE)];
    int encodedBytes = util::BitPacking::encode(posDeltaBuffer_, BUFFER_SIZE, encoded);
    posOut_->writeBytes(encoded, encodedBytes);

    posBufferPos_ = 0;  // Reset buffer
}

void Lucene104PostingsWriter::maybeFlushSkipEntry() {
    // Create skip entry every SKIP_INTERVAL docs
    if (docsSinceLastSkip_ >= SKIP_INTERVAL) {
        SkipEntry entry;
        entry.doc = lastDocID_;  // Doc at start of NEXT block
        entry.docFP = docOut_->getFilePointer();
        entry.maxFreq = blockMaxFreq_;
        entry.maxNorm = blockMaxNorm_;

        skipEntries_.push_back(entry);

        // Reset for next block
        blockMaxFreq_ = 0;
        blockMaxNorm_ = 0;
        docsSinceLastSkip_ = 0;
        lastSkipDocFP_ = entry.docFP;
    }
}

void Lucene104PostingsWriter::writeSkipData() {
    if (skipEntries_.empty()) {
        // No skip data for small postings lists
        skipStartFP_ = -1;
        return;
    }

    // Record file pointer to start of skip data for this term
    skipStartFP_ = skipOut_->getFilePointer();

    // Write number of skip entries
    skipOut_->writeVInt(static_cast<int32_t>(skipEntries_.size()));

    // Write skip entries with delta encoding
    int32_t lastDoc = 0;
    int64_t lastDocFP = docStartFP_;

    for (const auto& entry : skipEntries_) {
        // Delta encode doc ID
        int32_t docDelta = entry.doc - lastDoc;
        skipOut_->writeVInt(docDelta);

        // Delta encode file pointer
        int64_t docFPDelta = entry.docFP - lastDocFP;
        skipOut_->writeVLong(docFPDelta);

        // Write impact metadata
        skipOut_->writeVInt(entry.maxFreq);
        skipOut_->writeByte(static_cast<uint8_t>(entry.maxNorm));

        lastDoc = entry.doc;
        lastDocFP = entry.docFP;
    }
}

void Lucene104PostingsWriter::close() {
    // Close in-memory buffers (FieldsConsumer will write to actual files)
    if (docOut_) {
        docOut_->close();
        docOut_.reset();
    }
    if (skipOut_) {
        skipOut_->close();
        skipOut_.reset();
    }
    if (posOut_) {
        posOut_->close();
        posOut_.reset();
    }
}

int64_t Lucene104PostingsWriter::getFilePointer() const {
    return docOut_ ? docOut_->getFilePointer() : 0;
}

std::vector<uint8_t> Lucene104PostingsWriter::getBytes() const {
    // Cast to ByteBuffersIndexOutput to access toArrayCopy()
    auto* bufOut = dynamic_cast<store::ByteBuffersIndexOutput*>(docOut_.get());
    if (bufOut) {
        return bufOut->toArrayCopy();
    }
    return std::vector<uint8_t>();
}

std::vector<uint8_t> Lucene104PostingsWriter::getSkipBytes() const {
    // Cast to ByteBuffersIndexOutput to access toArrayCopy()
    auto* bufOut = dynamic_cast<store::ByteBuffersIndexOutput*>(skipOut_.get());
    if (bufOut) {
        return bufOut->toArrayCopy();
    }
    return std::vector<uint8_t>();
}

std::vector<uint8_t> Lucene104PostingsWriter::getPositionBytes() const {
    // Cast to ByteBuffersIndexOutput to access toArrayCopy()
    auto* bufOut = dynamic_cast<store::ByteBuffersIndexOutput*>(posOut_.get());
    if (bufOut) {
        return bufOut->toArrayCopy();
    }
    return std::vector<uint8_t>();
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
