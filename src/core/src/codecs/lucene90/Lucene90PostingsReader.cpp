// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/Lucene90PostingsReader.h"

#include "diagon/codecs/CodecUtil.h"

#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene90 {

// ==================== Lucene90PostingsReader ====================

// Postings sub-header codec names (from Lucene90PostingsFormat.java)
static constexpr const char* TERMS_CODEC = "Lucene90PostingsWriterTerms";
static constexpr const char* DOC_CODEC = "Lucene90PostingsWriterDoc";
static constexpr const char* POS_CODEC = "Lucene90PostingsWriterPos";
static constexpr int32_t POSTINGS_VERSION_START = 0;
static constexpr int32_t POSTINGS_VERSION_CURRENT = 1;

Lucene90PostingsReader::Lucene90PostingsReader(index::SegmentReadState& state)
    : docIn_(nullptr)
    , posIn_(nullptr)
    , segmentName_(state.segmentName)
    , segmentSuffix_(state.segmentSuffix) {
    // Inputs set externally via setDocInput/setPosInput,
    // or via init() when called by Lucene90BlockTreeTermsReader.
}

Lucene90PostingsReader::~Lucene90PostingsReader() {
    try {
        close();
    } catch (...) {
    }
}

std::unique_ptr<index::PostingsEnum>
Lucene90PostingsReader::postings(const index::FieldInfo& fieldInfo,
                                 const lucene104::TermState& termState,
                                 bool /*useBatch*/) {
    Lucene90TermState ts;
    ts.docStartFP = termState.docStartFP;
    ts.posStartFP = termState.posStartFP;
    ts.docFreq = termState.docFreq;
    ts.totalTermFreq = termState.totalTermFreq;
    return postings(fieldInfo, ts);
}

std::unique_ptr<index::PostingsEnum>
Lucene90PostingsReader::postingsWithPositions(const index::FieldInfo& fieldInfo,
                                              const lucene104::TermState& termState) {
    Lucene90TermState ts;
    ts.docStartFP = termState.docStartFP;
    ts.posStartFP = termState.posStartFP;
    ts.docFreq = termState.docFreq;
    ts.totalTermFreq = termState.totalTermFreq;
    return postingsWithPositions(fieldInfo, ts);
}

std::unique_ptr<index::PostingsEnum>
Lucene90PostingsReader::impactsPostings(const index::FieldInfo& fieldInfo,
                                        const lucene104::TermState& termState) {
    // MVP: fall back to basic postings (no WAND skip support yet)
    return postings(fieldInfo, termState, false);
}

std::unique_ptr<index::PostingsEnum>
Lucene90PostingsReader::postings(const index::FieldInfo& fieldInfo,
                                 const Lucene90TermState& termState) {
    if (!docIn_) {
        throw std::runtime_error("No doc input set for Lucene90PostingsReader");
    }
    bool indexHasFreq = (fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS);
    return std::make_unique<Lucene90BlockDocsEnum>(docIn_->clone(), termState, indexHasFreq);
}

std::unique_ptr<index::PostingsEnum>
Lucene90PostingsReader::postingsWithPositions(const index::FieldInfo& fieldInfo,
                                              const Lucene90TermState& termState) {
    if (!docIn_) {
        throw std::runtime_error("No doc input set for Lucene90PostingsReader");
    }
    if (termState.posStartFP < 0 || !posIn_) {
        return postings(fieldInfo, termState);
    }
    bool indexHasFreq = (fieldInfo.indexOptions >= index::IndexOptions::DOCS_AND_FREQS);
    return std::make_unique<Lucene90BlockPosEnum>(
        docIn_->clone(), posIn_->clone(), termState, indexHasFreq);
}

void Lucene90PostingsReader::init(store::IndexInput& metaIn, index::SegmentReadState& state) {
    // Read postings sub-header from .tmd (the terms dict meta file)
    // This validates that the postings format version matches
    CodecUtil::checkIndexHeader(metaIn, TERMS_CODEC,
                                POSTINGS_VERSION_START, POSTINGS_VERSION_CURRENT,
                                state.segmentID, state.segmentSuffix);
    int blockSize = metaIn.readVInt();
    if (blockSize != 128) {
        throw std::runtime_error(
            "Lucene90PostingsReader: expected block size 128, got " + std::to_string(blockSize));
    }

    // Open .doc file
    std::string docFile = state.segmentName + ".doc";
    try {
        docIn_ = state.directory->openInput(docFile, store::IOContext::READ);
        CodecUtil::checkIndexHeader(*docIn_, DOC_CODEC,
                                    POSTINGS_VERSION_START, POSTINGS_VERSION_CURRENT,
                                    state.segmentID, state.segmentSuffix);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to open .doc file: " + std::string(e.what()));
    }

    // Open .pos file if positions are indexed
    if (state.fieldInfos.hasProx()) {
        std::string posFile = state.segmentName + ".pos";
        try {
            posIn_ = state.directory->openInput(posFile, store::IOContext::READ);
            CodecUtil::checkIndexHeader(*posIn_, POS_CODEC,
                                        POSTINGS_VERSION_START, POSTINGS_VERSION_CURRENT,
                                        state.segmentID, state.segmentSuffix);
        } catch (const std::exception&) {
            // .pos file optional (no positions indexed)
        }
    }
}

void Lucene90PostingsReader::close() {
    docIn_.reset();
    posIn_.reset();
}

// ==================== Lucene90BlockDocsEnum ====================

Lucene90BlockDocsEnum::Lucene90BlockDocsEnum(
    std::unique_ptr<store::IndexInput> docIn,
    const Lucene90TermState& termState, bool indexHasFreq)
    : docIn_(std::move(docIn))
    , forUtil_()
    , pforUtil_(forUtil_)
    , docFreq_(termState.docFreq)
    , totalTermFreq_(termState.totalTermFreq)
    , indexHasFreq_(indexHasFreq)
    , singletonDocID_(termState.singletonDocID)
    , doc_(-1)
    , freq_(1)
    , docBuffer_{}
    , freqBuffer_{}
    , docBufferUpto_(BLOCK_SIZE)
    , blockUpto_(0)
    , accum_(0) {
    if (!indexHasFreq_) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            freqBuffer_[i] = 1;
        }
    }
    if (docFreq_ > 1) {
        docIn_->seek(termState.docStartFP);
    }
}

int Lucene90BlockDocsEnum::nextDoc() {
    if (docBufferUpto_ == BLOCK_SIZE) {
        if (blockUpto_ == docFreq_) {
            return doc_ = NO_MORE_DOCS;
        }
        refillDocs();
    }
    doc_ = static_cast<int>(docBuffer_[docBufferUpto_]);
    freq_ = static_cast<int>(freqBuffer_[docBufferUpto_]);
    docBufferUpto_++;
    return doc_;
}

int Lucene90BlockDocsEnum::advance(int target) {
    while (doc_ < target) {
        if (nextDoc() == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }
    }
    return doc_;
}

void Lucene90BlockDocsEnum::refillDocs() {
    const int left = docFreq_ - blockUpto_;

    if (left >= BLOCK_SIZE) {
        // Full PFOR block: decode doc deltas with prefix sum
        pforUtil_.decodeAndPrefixSum(*docIn_, accum_, docBuffer_);
        // Decode freq block (separate PFOR block in Lucene90 format)
        if (indexHasFreq_) {
            pforUtil_.decode(*docIn_, freqBuffer_);
        }
        blockUpto_ += BLOCK_SIZE;
    } else if (docFreq_ == 1) {
        // Singleton: doc ID and freq stored in TermState
        docBuffer_[0] = singletonDocID_;
        freqBuffer_[0] = totalTermFreq_;
        docBuffer_[1] = NO_MORE_DOCS;
        blockUpto_++;
    } else {
        // VInt tail: low-bit freq packing + prefix sum
        readVIntBlock(*docIn_, docBuffer_, freqBuffer_, left, indexHasFreq_);
        prefixSum(docBuffer_, left, accum_);
        docBuffer_[left] = NO_MORE_DOCS;
        blockUpto_ += left;
    }
    accum_ = docBuffer_[BLOCK_SIZE - 1];
    docBufferUpto_ = 0;
}

void Lucene90BlockDocsEnum::readVIntBlock(store::IndexInput& in, int64_t* docBuffer,
                                          int64_t* freqBuffer, int num,
                                          bool indexHasFreq) {
    if (indexHasFreq) {
        for (int i = 0; i < num; ++i) {
            const int code = in.readVInt();
            docBuffer[i] = static_cast<uint32_t>(code) >> 1;
            if (code & 1) {
                freqBuffer[i] = 1;
            } else {
                freqBuffer[i] = in.readVInt();
            }
        }
    } else {
        for (int i = 0; i < num; ++i) {
            docBuffer[i] = in.readVInt();
        }
    }
}

void Lucene90BlockDocsEnum::prefixSum(int64_t* buffer, int count, int64_t base) {
    buffer[0] += base;
    for (int i = 1; i < count; ++i) {
        buffer[i] += buffer[i - 1];
    }
}

// ==================== Lucene90BlockPosEnum ====================

Lucene90BlockPosEnum::Lucene90BlockPosEnum(
    std::unique_ptr<store::IndexInput> docIn,
    std::unique_ptr<store::IndexInput> posIn,
    const Lucene90TermState& termState, bool indexHasFreq)
    : docIn_(std::move(docIn))
    , posIn_(std::move(posIn))
    , forUtil_()
    , pforUtil_(forUtil_)
    , docFreq_(termState.docFreq)
    , totalTermFreq_(termState.totalTermFreq)
    , indexHasFreq_(indexHasFreq)
    , singletonDocID_(termState.singletonDocID)
    , doc_(-1)
    , freq_(1)
    , position_(0)
    , docBuffer_{}
    , freqBuffer_{}
    , docBufferUpto_(BLOCK_SIZE)
    , blockUpto_(0)
    , accum_(0)
    , posDeltaBuffer_{}
    , posBufUpto_(BLOCK_SIZE)
    , posPendingCount_(0)
    , lastPosBlockFP_(-1)
    , totalPosRead_(0) {
    if (!indexHasFreq_) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            freqBuffer_[i] = 1;
        }
    }
    if (docFreq_ > 1) {
        docIn_->seek(termState.docStartFP);
    }
    if (termState.posStartFP >= 0) {
        posIn_->seek(termState.posStartFP);
        if (termState.lastPosBlockOffset >= 0) {
            lastPosBlockFP_ = termState.posStartFP + termState.lastPosBlockOffset;
        }
    }
}

int Lucene90BlockPosEnum::nextDoc() {
    skipPositions();

    if (docBufferUpto_ == BLOCK_SIZE) {
        if (blockUpto_ == docFreq_) {
            return doc_ = NO_MORE_DOCS;
        }
        refillDocs();
    }
    doc_ = static_cast<int>(docBuffer_[docBufferUpto_]);
    freq_ = static_cast<int>(freqBuffer_[docBufferUpto_]);
    docBufferUpto_++;

    posPendingCount_ = freq_;
    position_ = 0;

    return doc_;
}

int Lucene90BlockPosEnum::advance(int target) {
    while (doc_ < target) {
        if (nextDoc() == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }
    }
    return doc_;
}

int Lucene90BlockPosEnum::nextPosition() {
    if (posPendingCount_ <= 0) {
        return -1;
    }
    if (posBufUpto_ == BLOCK_SIZE) {
        refillPositions();
        posBufUpto_ = 0;
    }

    position_ += static_cast<int>(posDeltaBuffer_[posBufUpto_++]);
    posPendingCount_--;
    totalPosRead_++;
    return position_;
}

void Lucene90BlockPosEnum::refillDocs() {
    const int left = docFreq_ - blockUpto_;

    if (left >= BLOCK_SIZE) {
        pforUtil_.decodeAndPrefixSum(*docIn_, accum_, docBuffer_);
        if (indexHasFreq_) {
            pforUtil_.decode(*docIn_, freqBuffer_);
        }
        blockUpto_ += BLOCK_SIZE;
    } else if (docFreq_ == 1) {
        docBuffer_[0] = singletonDocID_;
        freqBuffer_[0] = totalTermFreq_;
        docBuffer_[1] = NO_MORE_DOCS;
        blockUpto_++;
    } else {
        readVIntBlock(*docIn_, docBuffer_, freqBuffer_, left, indexHasFreq_);
        prefixSum(docBuffer_, left, accum_);
        docBuffer_[left] = NO_MORE_DOCS;
        blockUpto_ += left;
    }
    accum_ = docBuffer_[BLOCK_SIZE - 1];
    docBufferUpto_ = 0;
}

void Lucene90BlockPosEnum::refillPositions() {
    if (lastPosBlockFP_ >= 0 && posIn_->getFilePointer() == lastPosBlockFP_) {
        // VInt tail for positions
        const int count = static_cast<int>(totalTermFreq_ % BLOCK_SIZE);
        for (int i = 0; i < count; ++i) {
            posDeltaBuffer_[i] = posIn_->readVInt();
        }
    } else {
        // Full PFOR block
        pforUtil_.decode(*posIn_, posDeltaBuffer_);
    }
}

void Lucene90BlockPosEnum::skipPositions() {
    while (posPendingCount_ > 0) {
        if (posBufUpto_ == BLOCK_SIZE) {
            refillPositions();
            posBufUpto_ = 0;
        }
        posBufUpto_++;
        posPendingCount_--;
        totalPosRead_++;
    }
}

void Lucene90BlockPosEnum::readVIntBlock(store::IndexInput& in, int64_t* docBuffer,
                                         int64_t* freqBuffer, int num,
                                         bool indexHasFreq) {
    if (indexHasFreq) {
        for (int i = 0; i < num; ++i) {
            const int code = in.readVInt();
            docBuffer[i] = static_cast<uint32_t>(code) >> 1;
            if (code & 1) {
                freqBuffer[i] = 1;
            } else {
                freqBuffer[i] = in.readVInt();
            }
        }
    } else {
        for (int i = 0; i < num; ++i) {
            docBuffer[i] = in.readVInt();
        }
    }
}

void Lucene90BlockPosEnum::prefixSum(int64_t* buffer, int count, int64_t base) {
    buffer[0] += base;
    for (int i = 1; i < count; ++i) {
        buffer[i] += buffer[i - 1];
    }
}

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
