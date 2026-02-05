// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"

#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/Exceptions.h"
#include "diagon/util/StreamVByte.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// File extensions
static const std::string DOC_EXTENSION = "doc";
static const std::string SKIP_EXTENSION = "skp";

Lucene104PostingsWriter::Lucene104PostingsWriter(index::SegmentWriteState& state)
    : docOut_(nullptr)
    , skipOut_(nullptr)
    , indexOptions_(index::IndexOptions::DOCS)
    , writeFreqs_(false)
    , docStartFP_(0)
    , skipStartFP_(-1)
    , lastDocID_(0)
    , docCount_(0)
    , totalTermFreq_(0)
    , segmentName_(state.segmentName)
    , segmentSuffix_(state.segmentSuffix)
    , docDeltaBuffer_{}  // Zero-initialize
    , freqBuffer_{}      // Zero-initialize
    , bufferPos_(0)
    , blockMaxFreq_(0)
    , blockMaxNorm_(0)
    , docsSinceLastSkip_(0)
    , lastSkipDocFP_(0)
    , lastSkipDoc_(0) {

    // Create .doc output file
    std::string docFileName = segmentName_;
    if (!segmentSuffix_.empty()) {
        docFileName += "_" + segmentSuffix_;
    }
    docFileName += "." + DOC_EXTENSION;

    // Create .skp output file (for skip entries with impacts)
    std::string skipFileName = segmentName_;
    if (!segmentSuffix_.empty()) {
        skipFileName += "_" + segmentSuffix_;
    }
    skipFileName += "." + SKIP_EXTENSION;

    // For now, use in-memory buffers
    // TODO: Use actual file via state.directory->createOutput()
    docOut_ = std::make_unique<store::ByteBuffersIndexOutput>(docFileName);
    skipOut_ = std::make_unique<store::ByteBuffersIndexOutput>(skipFileName);
}

Lucene104PostingsWriter::~Lucene104PostingsWriter() {
    // Ensure close() was called
    if (docOut_ || skipOut_) {
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

    // TODO Phase 2.1: Support positions, offsets, payloads
    // bool writePositions = (indexOptions >= IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    // bool writeOffsets = (indexOptions >= IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS);
}

void Lucene104PostingsWriter::startTerm() {
    // Record file pointer at start of this term's postings
    docStartFP_ = docOut_->getFilePointer();
    skipStartFP_ = -1;  // Will be set when first skip entry is written
    lastDocID_ = 0;
    docCount_ = 0;
    totalTermFreq_ = 0;
    bufferPos_ = 0;  // Reset StreamVByte buffer

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

    // Flush when buffer is full
    if (bufferPos_ >= BUFFER_SIZE) {
        flushBuffer();
    }
}

TermState Lucene104PostingsWriter::finishTerm() {
    // Flush any remaining buffered data
    if (bufferPos_ > 0) {
        // If we have fewer than 4 docs remaining, use VInt fallback
        for (int i = 0; i < bufferPos_; ++i) {
            docOut_->writeVInt(static_cast<int32_t>(docDeltaBuffer_[i]));
            if (writeFreqs_) {
                docOut_->writeVInt(static_cast<int32_t>(freqBuffer_[i]));
            }
        }
        bufferPos_ = 0;
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
    state.skipStartFP = skipStartFP_;
    state.docFreq = docCount_;
    state.totalTermFreq = totalTermFreq_;
    state.skipEntryCount = static_cast<int>(skipEntries_.size());

    return state;
}

void Lucene104PostingsWriter::flushBuffer() {
    if (bufferPos_ != BUFFER_SIZE) {
        return;  // Only flush when buffer is full (4 docs)
    }

    // Encode doc deltas using StreamVByte
    uint8_t docDeltaEncoded[17];  // Max: 1 control + 4*4 data bytes
    int docDeltaBytes = util::StreamVByte::encode(docDeltaBuffer_, BUFFER_SIZE, docDeltaEncoded);
    docOut_->writeBytes(docDeltaEncoded, docDeltaBytes);

    // Encode frequencies using StreamVByte (if required)
    if (writeFreqs_) {
        uint8_t freqEncoded[17];
        int freqBytes = util::StreamVByte::encode(freqBuffer_, BUFFER_SIZE, freqEncoded);
        docOut_->writeBytes(freqEncoded, freqBytes);
    }

    bufferPos_ = 0;  // Reset buffer
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
    if (docOut_) {
        docOut_->close();
        docOut_.reset();
    }
    if (skipOut_) {
        skipOut_->close();
        skipOut_.reset();
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

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
