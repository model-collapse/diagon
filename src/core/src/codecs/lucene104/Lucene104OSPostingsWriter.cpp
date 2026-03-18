// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104OSPostingsWriter.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// Zero segment ID placeholder (proper segment ID plumbing is a future task)
static const uint8_t ZERO_SEGMENT_ID[16] = {};

// ==================== Constructor/Destructor ====================

Lucene104OSPostingsWriter::Lucene104OSPostingsWriter(index::SegmentWriteState& state)
    : forUtil_()
    , pforUtil_(forUtil_)
    , indexOptions_(index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS)
    , writeFreqs_(true)
    , writePositions_(true)
    , fieldHasNorms_(false)
    , docStartFP_(0)
    , posStartFP_(0)
    , lastDocID_(-1)
    , docID_(0)
    , docCount_(0)
    , lastPosition_(0)
    , level0LastDocID_(-1)
    , level0LastPosFP_(0)
    , level1LastDocID_(-1)
    , level1LastPosFP_(0)
    , docBufferUpto_(0)
    , posBufferUpto_(0)
    , maxNumImpactsAtLevel0_(0)
    , maxImpactNumBytesAtLevel0_(0)
    , maxNumImpactsAtLevel1_(0)
    , maxImpactNumBytesAtLevel1_(0)
    , state_(state) {
    segmentName_ = state.segmentName;

    // Build file names
    docFileName_ = segmentName_;
    if (!state.segmentSuffix.empty()) {
        docFileName_ += "_" + state.segmentSuffix;
    }
    docFileName_ += ".doc";

    metaFileName_ = segmentName_;
    if (!state.segmentSuffix.empty()) {
        metaFileName_ += "_" + state.segmentSuffix;
    }
    metaFileName_ += ".psm";

    // Create .doc output
    docOut_ = state.directory->createOutput(docFileName_, store::IOContext::DEFAULT);
    CodecUtil::writeIndexHeader(*docOut_, DOC_CODEC, VERSION_CURRENT,
                                ZERO_SEGMENT_ID, state.segmentSuffix);

    // Create .psm (meta) output
    metaOut_ = state.directory->createOutput(metaFileName_, store::IOContext::DEFAULT);
    CodecUtil::writeIndexHeader(*metaOut_, META_CODEC, VERSION_CURRENT,
                                ZERO_SEGMENT_ID, state.segmentSuffix);

    // Create .pos output if any field has positions
    if (state.fieldInfos.hasProx()) {
        posFileName_ = segmentName_;
        if (!state.segmentSuffix.empty()) {
            posFileName_ += "_" + state.segmentSuffix;
        }
        posFileName_ += ".pos";

        posOut_ = state.directory->createOutput(posFileName_, store::IOContext::DEFAULT);
        CodecUtil::writeIndexHeader(*posOut_, POS_CODEC, VERSION_CURRENT,
                                    ZERO_SEGMENT_ID, state.segmentSuffix);
    }

    // Initialize buffers
    std::memset(docDeltaBuffer_, 0, sizeof(docDeltaBuffer_));
    std::memset(freqBuffer_, 0, sizeof(freqBuffer_));
    std::memset(posDeltaBuffer_, 0, sizeof(posDeltaBuffer_));

    // Reserve scratch buffers
    level0Buf_.reserve(4096);
    level1Buf_.reserve(64 * 1024);
    scratchBuf_.reserve(512);
}

Lucene104OSPostingsWriter::~Lucene104OSPostingsWriter() {
    try {
        close();
    } catch (...) {
        // Suppress in destructor
    }
}

// ==================== Init / SetField ====================

void Lucene104OSPostingsWriter::init(store::IndexOutput& termsOut) {
    CodecUtil::writeIndexHeader(termsOut, TERMS_CODEC, VERSION_CURRENT,
                                ZERO_SEGMENT_ID, state_.segmentSuffix);
    termsOut.writeVInt(BLOCK_SIZE);
}

void Lucene104OSPostingsWriter::setField(const index::FieldInfo& fieldInfo) {
    indexOptions_ = fieldInfo.indexOptions;
    writeFreqs_ = indexOptions_ >= index::IndexOptions::DOCS_AND_FREQS;
    writePositions_ = indexOptions_ >= index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    fieldHasNorms_ = fieldInfo.hasNorms();

    // Reset encoded term state
    lastEncodedState_ = OSTermState();
}

// ==================== Per-Term Methods ====================

void Lucene104OSPostingsWriter::startTerm() {
    docStartFP_ = docOut_->getFilePointer();
    if (writePositions_ && posOut_) {
        posStartFP_ = posOut_->getFilePointer();
        level0LastPosFP_ = posStartFP_;
        level1LastPosFP_ = posStartFP_;
    }
    lastDocID_ = -1;
    level0LastDocID_ = -1;
    level1LastDocID_ = -1;
    docBufferUpto_ = 0;
    posBufferUpto_ = 0;
    docCount_ = 0;
    lastPosition_ = 0;

    if (writeFreqs_) {
        level0Impacts_.clear();
        level1Impacts_.clear();
    }
}

void Lucene104OSPostingsWriter::startDoc(int docID, int freq) {
    if (docBufferUpto_ == BLOCK_SIZE) {
        flushDocBlock(false);
        docBufferUpto_ = 0;
    }

    const int docDelta = docID - lastDocID_;
    assert(docDelta > 0);

    docDeltaBuffer_[docBufferUpto_] = docDelta;
    if (writeFreqs_) {
        freqBuffer_[docBufferUpto_] = freq;
    }

    docID_ = docID;
    lastPosition_ = 0;

    if (writeFreqs_) {
        int64_t norm = 1;  // Default norm
        addImpact(level0Impacts_, freq, norm);
    }
}

void Lucene104OSPostingsWriter::addPosition(int position) {
    posDeltaBuffer_[posBufferUpto_] = position - lastPosition_;
    posBufferUpto_++;
    lastPosition_ = position;

    if (posBufferUpto_ == BLOCK_SIZE) {
        if (posOut_) {
            pforUtil_.encode(posDeltaBuffer_, *posOut_);
        }
        posBufferUpto_ = 0;
    }
}

void Lucene104OSPostingsWriter::finishDoc() {
    docBufferUpto_++;
    docCount_++;
    lastDocID_ = docID_;
}

// ==================== Finish Term ====================

OSTermState Lucene104OSPostingsWriter::finishTerm() {
    OSTermState state;
    state.docFreq = docCount_;

    // Singleton optimization: if only 1 doc, pulse docID into term dictionary
    if (docCount_ == 1) {
        state.singletonDocID = docDeltaBuffer_[0] - 1;
    } else {
        state.singletonDocID = -1;
        if (docBufferUpto_ > 0) {
            flushDocBlock(true);
        }
    }

    // Handle remaining positions (VInt tail)
    int64_t lastPosBlockOffset = -1;
    if (writePositions_ && posOut_) {
        if (posBufferUpto_ > 0) {
            for (int i = 0; i < posBufferUpto_; i++) {
                posOut_->writeVInt(posDeltaBuffer_[i]);
            }
        }
    }

    state.docStartFP = docStartFP_;
    state.posStartFP = posStartFP_;
    state.lastPosBlockOffset = lastPosBlockOffset;

    // Reset
    docBufferUpto_ = 0;
    posBufferUpto_ = 0;
    lastDocID_ = -1;
    docCount_ = 0;
    lastPosition_ = 0;

    return state;
}

// ==================== Encode Term Metadata ====================

void Lucene104OSPostingsWriter::encodeTerm(store::IndexOutput& out, const OSTermState& state,
                                            bool absolute) {
    if (absolute) {
        lastEncodedState_ = OSTermState();
    }

    if (lastEncodedState_.singletonDocID != -1 && state.singletonDocID != -1
        && state.docStartFP == lastEncodedState_.docStartFP) {
        int64_t delta = static_cast<int64_t>(state.singletonDocID) - lastEncodedState_.singletonDocID;
        int64_t zigzag = (delta << 1) ^ (delta >> 63);
        out.writeVLong((zigzag << 1) | 0x01);
    } else {
        out.writeVLong((state.docStartFP - lastEncodedState_.docStartFP) << 1);
        if (state.singletonDocID != -1) {
            out.writeVInt(state.singletonDocID);
        }
    }

    if (writePositions_) {
        out.writeVLong(state.posStartFP - lastEncodedState_.posStartFP);
    }

    if (writePositions_ && state.lastPosBlockOffset != -1) {
        out.writeVLong(state.lastPosBlockOffset);
    }

    lastEncodedState_ = state;
}

// ==================== Flush Doc Block ====================

void Lucene104OSPostingsWriter::flushDocBlock(bool finishTerm) {
    assert(docBufferUpto_ != 0);

    if (docBufferUpto_ < BLOCK_SIZE) {
        assert(finishTerm);
        // VInt tail block
        if (writeFreqs_) {
            for (int i = 0; i < docBufferUpto_; i++) {
                int delta = docDeltaBuffer_[i];
                int freq = freqBuffer_[i];
                if (freq == 1) {
                    writeVIntToBuffer(level0Buf_, (delta << 1) | 1);
                } else {
                    writeVIntToBuffer(level0Buf_, delta << 1);
                    writeVIntToBuffer(level0Buf_, freq);
                }
            }
        } else {
            for (int i = 0; i < docBufferUpto_; i++) {
                writeVIntToBuffer(level0Buf_, docDeltaBuffer_[i]);
            }
        }
    } else {
        // Full packed block (256 docs)
        level0Buf_.clear();

        if (writeFreqs_) {
            auto impacts = getCompetitiveImpacts(level0Impacts_);
            if (static_cast<int>(impacts.size()) > maxNumImpactsAtLevel0_) {
                maxNumImpactsAtLevel0_ = static_cast<int>(impacts.size());
            }

            scratchBuf_.clear();
            writeImpacts(impacts, scratchBuf_);
            if (static_cast<int>(scratchBuf_.size()) > maxImpactNumBytesAtLevel0_) {
                maxImpactNumBytesAtLevel0_ = static_cast<int>(scratchBuf_.size());
            }

            writeVLongToBuffer(level0Buf_, static_cast<int64_t>(scratchBuf_.size()));
            level0Buf_.insert(level0Buf_.end(), scratchBuf_.begin(), scratchBuf_.end());
            scratchBuf_.clear();

            if (writePositions_ && posOut_) {
                writeVLongToBuffer(level0Buf_, posOut_->getFilePointer() - level0LastPosFP_);
                level0Buf_.push_back(static_cast<uint8_t>(posBufferUpto_));
                level0LastPosFP_ = posOut_->getFilePointer();
            }
        }

        // Compute bitsPerValue for doc deltas
        int32_t orAll = 0;
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            orAll |= docDeltaBuffer_[i];
        }
        assert(orAll != 0);
        int bitsPerValue = 32 - __builtin_clz(static_cast<uint32_t>(orAll));

        level0Buf_.push_back(static_cast<uint8_t>(bitsPerValue));

        // Encode doc deltas using ForUtil to temp buffer
        store::ByteBuffersIndexOutput tempOut("temp_forutil");
        forUtil_.encode(docDeltaBuffer_, bitsPerValue, tempOut);
        const auto& tempBytes = tempOut.toArrayCopy();
        level0Buf_.insert(level0Buf_.end(), tempBytes.begin(), tempBytes.end());

        // Encode frequencies using PForUtil
        if (writeFreqs_) {
            store::ByteBuffersIndexOutput freqTempOut("temp_pforutil");
            pforUtil_.encode(freqBuffer_, freqTempOut);
            const auto& freqBytes = freqTempOut.toArrayCopy();
            level0Buf_.insert(level0Buf_.end(), freqBytes.begin(), freqBytes.end());
        }

        // Prepend skip0 header
        scratchBuf_.clear();
        writeVInt15ToBuffer(scratchBuf_, docID_ - level0LastDocID_);
        writeVLong15ToBuffer(scratchBuf_, static_cast<int64_t>(level0Buf_.size()));

        int64_t numSkipBytes = static_cast<int64_t>(level0Buf_.size())
                               + static_cast<int64_t>(scratchBuf_.size());

        writeVLongToBuffer(level1Buf_, numSkipBytes);
        level1Buf_.insert(level1Buf_.end(), scratchBuf_.begin(), scratchBuf_.end());
        scratchBuf_.clear();
    }

    level1Buf_.insert(level1Buf_.end(), level0Buf_.begin(), level0Buf_.end());
    level0Buf_.clear();

    level0LastDocID_ = docID_;

    if (writeFreqs_) {
        for (const auto& impact : level0Impacts_) {
            addImpact(level1Impacts_, impact.freq, impact.norm);
        }
        level0Impacts_.clear();
    }

    if ((docCount_ & LEVEL1_MASK) == 0) {
        writeLevel1SkipData();
        level1LastDocID_ = docID_;
        level1Impacts_.clear();
    } else if (finishTerm) {
        docOut_->writeBytes(level1Buf_.data(), level1Buf_.size());
        level1Buf_.clear();
        level1Impacts_.clear();
    }
}

// ==================== Level1 Skip Data ====================

void Lucene104OSPostingsWriter::writeLevel1SkipData() {
    docOut_->writeVInt(docID_ - level1LastDocID_);

    if (writeFreqs_) {
        auto impacts = getCompetitiveImpacts(level1Impacts_);
        if (static_cast<int>(impacts.size()) > maxNumImpactsAtLevel1_) {
            maxNumImpactsAtLevel1_ = static_cast<int>(impacts.size());
        }

        scratchBuf_.clear();
        writeImpacts(impacts, scratchBuf_);
        int64_t numImpactBytes = static_cast<int64_t>(scratchBuf_.size());
        if (static_cast<int>(numImpactBytes) > maxImpactNumBytesAtLevel1_) {
            maxImpactNumBytesAtLevel1_ = static_cast<int>(numImpactBytes);
        }

        if (writePositions_ && posOut_) {
            writeVLongToBuffer(scratchBuf_, posOut_->getFilePointer() - level1LastPosFP_);
            scratchBuf_.push_back(static_cast<uint8_t>(posBufferUpto_));
            level1LastPosFP_ = posOut_->getFilePointer();
        }

        int64_t level1Len = 2 * 2 + static_cast<int64_t>(scratchBuf_.size())
                            + static_cast<int64_t>(level1Buf_.size());
        docOut_->writeVLong(level1Len);

        docOut_->writeShort(static_cast<int16_t>(scratchBuf_.size() + 2));
        docOut_->writeShort(static_cast<int16_t>(numImpactBytes));
        docOut_->writeBytes(scratchBuf_.data(), scratchBuf_.size());
        scratchBuf_.clear();
    } else {
        docOut_->writeVLong(static_cast<int64_t>(level1Buf_.size()));
    }

    docOut_->writeBytes(level1Buf_.data(), level1Buf_.size());
    level1Buf_.clear();
}

// ==================== Close ====================

void Lucene104OSPostingsWriter::close() {
    int64_t docFP = docOut_ ? docOut_->getFilePointer() : 0;
    int64_t posFP = posOut_ ? posOut_->getFilePointer() : 0;

    if (docOut_) {
        CodecUtil::writeFooter(*docOut_);
        docOut_->close();
        docOut_.reset();
    }
    if (posOut_) {
        CodecUtil::writeFooter(*posOut_);
        posOut_->close();
        posOut_.reset();
    }
    if (metaOut_) {
        metaOut_->writeInt(maxNumImpactsAtLevel0_);
        metaOut_->writeInt(maxImpactNumBytesAtLevel0_);
        metaOut_->writeInt(maxNumImpactsAtLevel1_);
        metaOut_->writeInt(maxImpactNumBytesAtLevel1_);
        metaOut_->writeLong(docFP);
        if (posFP > 0) {
            metaOut_->writeLong(posFP);
        }
        CodecUtil::writeFooter(*metaOut_);
        metaOut_->close();
        metaOut_.reset();
    }
}

// ==================== Impact Writing ====================

void Lucene104OSPostingsWriter::writeImpacts(const std::vector<Impact>& impacts,
                                              std::vector<uint8_t>& out) {
    int prevFreq = 0;
    int64_t prevNorm = 0;
    for (const auto& impact : impacts) {
        int freqDelta = impact.freq - prevFreq - 1;
        int64_t normDelta = impact.norm - prevNorm - 1;
        if (normDelta == 0) {
            writeVIntToBuffer(out, freqDelta << 1);
        } else {
            writeVIntToBuffer(out, (freqDelta << 1) | 1);
            int64_t zigzag = (normDelta << 1) ^ (normDelta >> 63);
            writeVLongToBuffer(out, zigzag);
        }
        prevFreq = impact.freq;
        prevNorm = impact.norm;
    }
}

void Lucene104OSPostingsWriter::addImpact(std::vector<Impact>& acc, int freq, int64_t norm) {
    acc.push_back({freq, norm});
}

std::vector<Lucene104OSPostingsWriter::Impact>
Lucene104OSPostingsWriter::getCompetitiveImpacts(const std::vector<Impact>& acc) {
    if (acc.empty()) {
        return {{1, 1}};
    }

    int maxFreq = 0;
    int64_t maxNorm = 0;
    for (const auto& impact : acc) {
        maxFreq = std::max(maxFreq, impact.freq);
        maxNorm = std::max(maxNorm, impact.norm);
    }
    return {{maxFreq, maxNorm}};
}

// ==================== Buffer Helpers ====================

void Lucene104OSPostingsWriter::writeVIntToBuffer(std::vector<uint8_t>& buf, int32_t v) {
    uint32_t uv = static_cast<uint32_t>(v);
    while ((uv & ~0x7Fu) != 0) {
        buf.push_back(static_cast<uint8_t>((uv & 0x7F) | 0x80));
        uv >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(uv));
}

void Lucene104OSPostingsWriter::writeVLongToBuffer(std::vector<uint8_t>& buf, int64_t v) {
    uint64_t uv = static_cast<uint64_t>(v);
    while ((uv & ~0x7FULL) != 0) {
        buf.push_back(static_cast<uint8_t>((uv & 0x7F) | 0x80));
        uv >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(uv));
}

void Lucene104OSPostingsWriter::writeVInt15ToBuffer(std::vector<uint8_t>& buf, int32_t v) {
    writeVLong15ToBuffer(buf, static_cast<int64_t>(v));
}

void Lucene104OSPostingsWriter::writeVLong15ToBuffer(std::vector<uint8_t>& buf, int64_t v) {
    assert(v >= 0);
    if ((v & ~0x7FFFLL) == 0) {
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v));
    } else {
        int16_t s = static_cast<int16_t>(0x8000 | (v & 0x7FFF));
        buf.push_back(static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8));
        buf.push_back(static_cast<uint8_t>(s));
        writeVLongToBuffer(buf, v >> 15);
    }
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
