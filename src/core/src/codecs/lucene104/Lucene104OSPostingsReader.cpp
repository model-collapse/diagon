// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104OSPostingsReader.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsWriter.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/Exceptions.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// Zero segment ID for header validation
static const uint8_t ZERO_SEGMENT_ID[16] = {};

// ==================== Lucene104OSPostingsReader ====================

Lucene104OSPostingsReader::Lucene104OSPostingsReader(index::SegmentReadState& state)
    : forUtil_(), pforUtil_(forUtil_) {
    // Open .doc file
    std::string docFileName = state.segmentName;
    if (!state.segmentSuffix.empty()) {
        docFileName += "_" + state.segmentSuffix;
    }
    docFileName += ".doc";

    docIn_ = state.directory->openInput(docFileName, store::IOContext::DEFAULT);
    CodecUtil::checkIndexHeader(*docIn_, Lucene104OSPostingsWriter::DOC_CODEC,
                                Lucene104OSPostingsWriter::VERSION_CURRENT,
                                Lucene104OSPostingsWriter::VERSION_CURRENT,
                                ZERO_SEGMENT_ID, state.segmentSuffix);

    // Open .pos file if it exists
    if (state.fieldInfos.hasProx()) {
        std::string posFileName = state.segmentName;
        if (!state.segmentSuffix.empty()) {
            posFileName += "_" + state.segmentSuffix;
        }
        posFileName += ".pos";

        posIn_ = state.directory->openInput(posFileName, store::IOContext::DEFAULT);
        CodecUtil::checkIndexHeader(*posIn_, Lucene104OSPostingsWriter::POS_CODEC,
                                    Lucene104OSPostingsWriter::VERSION_CURRENT,
                                    Lucene104OSPostingsWriter::VERSION_CURRENT,
                                    ZERO_SEGMENT_ID, state.segmentSuffix);
    }
}

Lucene104OSPostingsReader::~Lucene104OSPostingsReader() = default;

void Lucene104OSPostingsReader::init(store::IndexInput& termsIn) {
    CodecUtil::checkIndexHeader(termsIn, Lucene104OSPostingsWriter::TERMS_CODEC,
                                Lucene104OSPostingsWriter::VERSION_CURRENT,
                                Lucene104OSPostingsWriter::VERSION_CURRENT,
                                ZERO_SEGMENT_ID, "");
    int blockSize = termsIn.readVInt();
    if (blockSize != ForUtil::BLOCK_SIZE) {
        throw CorruptIndexException("Expected block size " +
                                    std::to_string(ForUtil::BLOCK_SIZE) +
                                    " but got " + std::to_string(blockSize));
    }
}

void Lucene104OSPostingsReader::decodeTerm(store::IndexInput& termsIn,
                                            const index::FieldInfo& fieldInfo,
                                            OSTermState& state, bool absolute) {
    if (absolute) {
        lastState_ = OSTermState();
    }

    int64_t l = termsIn.readVLong();
    if ((l & 0x01) == 1) {
        // Singleton delta encoding
        int64_t zigzag = l >> 1;
        int64_t delta = (zigzag >> 1) ^ -(zigzag & 1);
        state.singletonDocID = static_cast<int>(lastState_.singletonDocID + delta);
        state.docStartFP = lastState_.docStartFP;
    } else {
        state.docStartFP = lastState_.docStartFP + (l >> 1);
        if (state.docFreq == 1) {
            state.singletonDocID = termsIn.readVInt();
        } else {
            state.singletonDocID = -1;
        }
    }

    bool hasPositions = fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    if (hasPositions) {
        state.posStartFP = lastState_.posStartFP + termsIn.readVLong();
    }
    if (hasPositions && state.lastPosBlockOffset != -1) {
        state.lastPosBlockOffset = termsIn.readVLong();
    }

    lastState_ = state;
}

std::unique_ptr<index::PostingsEnum> Lucene104OSPostingsReader::postings(
    const index::FieldInfo& fieldInfo, const OSTermState& state, int flags) {
    bool readFreqs = fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS;
    bool readPositions = fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS
                         && (flags & index::FEATURE_POSITIONS);

    return std::make_unique<OSPostingsEnum>(
        *docIn_, posIn_.get(), state, readFreqs, readPositions);
}

// ==================== OSPostingsEnum ====================

OSPostingsEnum::OSPostingsEnum(store::IndexInput& docIn, store::IndexInput* posIn,
                               const OSTermState& state, bool readFreqs, bool readPositions)
    : docFreq_(state.docFreq)
    , totalTermFreq_(state.totalTermFreq)
    , singletonDocID_(state.singletonDocID)
    , readFreqs_(readFreqs)
    , readPositions_(readPositions)
    , docBufferUpto_(ForUtil::BLOCK_SIZE)
    , posBufferUpto_(ForUtil::BLOCK_SIZE)
    , doc_(-1)
    , freq_(1)
    , position_(-1)
    , docsRead_(0)
    , level0LastDocID_(-1)
    , level1LastDocID_(-1)
    , level0DocEndFP_(0)
    , forUtil_()
    , pforUtil_(forUtil_) {
    if (singletonDocID_ == -1) {
        docIn_ = docIn.clone();
        docIn_->seek(state.docStartFP);
    }

    if (readPositions && posIn) {
        posIn_ = posIn->clone();
        posIn_->seek(state.posStartFP);
    }

    std::memset(docDeltaBuffer_, 0, sizeof(docDeltaBuffer_));
    std::memset(freqBuffer_, 0, sizeof(freqBuffer_));
    std::memset(posDeltaBuffer_, 0, sizeof(posDeltaBuffer_));
}

int OSPostingsEnum::nextDoc() {
    if (singletonDocID_ != -1) {
        if (doc_ == -1) {
            doc_ = singletonDocID_;
            freq_ = static_cast<int>(totalTermFreq_);
            docsRead_ = 1;
            return doc_;
        }
        doc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    if (docsRead_ >= docFreq_) {
        doc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    if (docBufferUpto_ >= ForUtil::BLOCK_SIZE) {
        if (!refillDocBlock()) {
            doc_ = NO_MORE_DOCS;
            return NO_MORE_DOCS;
        }
    }

    doc_ += docDeltaBuffer_[docBufferUpto_];
    if (readFreqs_) {
        freq_ = freqBuffer_[docBufferUpto_];
    }
    docBufferUpto_++;
    docsRead_++;
    return doc_;
}

int OSPostingsEnum::advance(int target) {
    int doc;
    while ((doc = nextDoc()) < target) {
        if (doc == NO_MORE_DOCS) return NO_MORE_DOCS;
    }
    return doc;
}

int OSPostingsEnum::freq() const {
    return freq_;
}

int OSPostingsEnum::nextPosition() {
    if (!readPositions_ || !posIn_) {
        return -1;
    }

    if (posBufferUpto_ >= ForUtil::BLOCK_SIZE) {
        refillPosBlock();
    }

    position_ += posDeltaBuffer_[posBufferUpto_];
    posBufferUpto_++;
    return position_;
}

bool OSPostingsEnum::refillDocBlock() {
    if (!docIn_) return false;

    int remaining = docFreq_ - docsRead_;
    if (remaining <= 0) return false;

    if (remaining >= ForUtil::BLOCK_SIZE) {
        // Full packed block
        // Read skip data: VLong(numSkipBytes) is the total skip+block length
        int64_t numSkipBytes = docIn_->readVLong();
        int64_t skipEnd = docIn_->getFilePointer() + numSkipBytes;

        // Read VInt15(docDelta) + VLong15(blockLength)
        int16_t s = docIn_->readShort();
        if ((s & static_cast<int16_t>(0x8000)) != 0) {
            // More than 15 bits
            docIn_->readVInt();  // consume rest of VInt15
        }

        s = docIn_->readShort();
        int64_t blockLength;
        if ((s & static_cast<int16_t>(0x8000)) == 0) {
            blockLength = static_cast<int64_t>(s) & 0xFFFF;
        } else {
            blockLength = (static_cast<int64_t>(s) & 0x7FFF) | (docIn_->readVLong() << 15);
        }

        // Seek to block data start (skip past any remaining impact data)
        int64_t blockStart = skipEnd - blockLength;
        if (docIn_->getFilePointer() < blockStart) {
            docIn_->seek(blockStart);
        }

        // Read doc deltas
        int bitsPerValue = docIn_->readByte() & 0xFF;
        if (bitsPerValue == 0) {
            for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
                docDeltaBuffer_[i] = 1;
            }
        } else {
            forUtil_.decode(bitsPerValue, *docIn_, docDeltaBuffer_);
        }

        // Read freq block
        if (readFreqs_) {
            pforUtil_.decode(*docIn_, freqBuffer_);
        }
    } else {
        // VInt tail block
        readVIntBlock(remaining);
    }

    docBufferUpto_ = 0;
    return true;
}

void OSPostingsEnum::readVIntBlock(int count) {
    if (readFreqs_) {
        for (int i = 0; i < count; i++) {
            int code = docIn_->readVInt();
            docDeltaBuffer_[i] = code >> 1;
            if ((code & 1) != 0) {
                freqBuffer_[i] = 1;
            } else {
                freqBuffer_[i] = docIn_->readVInt();
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            docDeltaBuffer_[i] = docIn_->readVInt();
        }
    }
    for (int i = count; i < ForUtil::BLOCK_SIZE; i++) {
        docDeltaBuffer_[i] = 0;
        freqBuffer_[i] = 0;
    }
}

void OSPostingsEnum::skipLevel0SkipData() {
    // Already consumed in refillDocBlock
}

void OSPostingsEnum::skipLevel1SkipData() {
    // For sequential access, level1 header is read through
}

void OSPostingsEnum::refillPosBlock() {
    if (!posIn_) return;
    pforUtil_.decode(*posIn_, posDeltaBuffer_);
    posBufferUpto_ = 0;
    position_ = 0;
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
