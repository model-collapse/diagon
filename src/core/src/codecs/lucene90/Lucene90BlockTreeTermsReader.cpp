// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/Lucene90BlockTreeTermsReader.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene90 {

// ==================== Lucene Suffix Decompression ====================

/**
 * Streaming LZ4 decompressor matching Lucene's LZ4.decompress().
 * Reads tokens from the IndexInput until decompressedLen output bytes are produced.
 * The standard LZ4 library cannot be used because Lucene writes LZ4 blocks without
 * a compressed-size prefix — the format is self-delimiting based on decompressed size.
 */
static void decompressLZ4(store::IndexInput& in, uint8_t* dest, int decompressedLen) {
    int dOff = 0;
    const int destEnd = decompressedLen;

    while (dOff < destEnd) {
        // Read token: upper 4 bits = literal length, lower 4 bits = match length
        int token = in.readByte() & 0xFF;
        int literalLen = token >> 4;

        if (literalLen != 0) {
            if (literalLen == 0x0F) {
                uint8_t b;
                do {
                    b = in.readByte();
                    literalLen += (b & 0xFF);
                } while (b == static_cast<uint8_t>(0xFF));
            }
            in.readBytes(dest + dOff, literalLen);
            dOff += literalLen;
        }

        if (dOff >= destEnd) break;

        // Read 2-byte little-endian match offset
        int matchDec = (in.readByte() & 0xFF);
        matchDec |= (in.readByte() & 0xFF) << 8;

        if (matchDec == 0) {
            throw CorruptIndexException("LZ4: invalid match offset 0", in.toString());
        }

        int matchLen = token & 0x0F;
        if (matchLen == 0x0F) {
            uint8_t b;
            do {
                b = in.readByte();
                matchLen += (b & 0xFF);
            } while (b == static_cast<uint8_t>(0xFF));
        }
        matchLen += 4;  // Minimum match length is 4

        // Copy match (may overlap for RLE patterns)
        int ref = dOff - matchDec;
        int end = dOff + matchLen;
        if (end > destEnd) {
            throw CorruptIndexException("LZ4: match extends beyond output buffer", in.toString());
        }
        for (; dOff < end; ++dOff, ++ref) {
            dest[dOff] = dest[ref];
        }
    }
}

/**
 * LOWERCASE_ASCII decompressor matching Lucene's LowercaseAsciiCompression.decompress().
 * Packs 4 lowercase ASCII chars into 3 bytes (saving ~25%), with exceptions for
 * non-compressible bytes.
 */
static void decompressLowercaseAscii(store::IndexInput& in, uint8_t* out, int len) {
    const int saved = len >> 2;  // len / 4
    int compressedLen = len - saved;

    // 1. Read the packed bytes
    in.readBytes(out, compressedLen);

    // 2. Restore the leading 2 bits of each packed byte
    for (int i = 0; i < saved; ++i) {
        out[compressedLen + i] = static_cast<uint8_t>(
            ((out[i] & 0xC0) >> 2)
            | ((out[saved + i] & 0xC0) >> 4)
            | ((out[(saved << 1) + i] & 0xC0) >> 6));
    }

    // 3. Move back to original range [0x1F,0x3F) or [0x5F,0x7F)
    for (int i = 0; i < len; ++i) {
        uint8_t b = out[i];
        out[i] = static_cast<uint8_t>(((b & 0x1F) | 0x20 | ((b & 0x20) << 1)) - 1);
    }

    // 4. Restore exceptions
    int numExceptions = in.readVInt();
    int idx = 0;
    for (int e = 0; e < numExceptions; ++e) {
        idx += in.readByte() & 0xFF;  // Delta offset
        out[idx] = in.readByte();      // The exception byte
    }
}

// ==================== Helper: read BytesRef from DataInput ====================

static std::vector<uint8_t> readBytesRef(store::IndexInput& in) {
    int numBytes = in.readVInt();
    if (numBytes < 0) {
        throw CorruptIndexException("invalid bytes length: " + std::to_string(numBytes),
                                     in.toString());
    }
    std::vector<uint8_t> bytes(numBytes);
    if (numBytes > 0) {
        in.readBytes(bytes.data(), numBytes);
    }
    return bytes;
}

// ==================== Lucene90BlockTreeTermsReader ====================

Lucene90BlockTreeTermsReader::Lucene90BlockTreeTermsReader(
    index::SegmentReadState& state,
    std::unique_ptr<Lucene90PostingsReader> postingsReader)
    : postingsReader_(std::move(postingsReader)) {

    const std::string& segment = state.segmentName;
    const std::string& suffix = state.segmentSuffix;
    const uint8_t* segID = state.segmentID;

    // Build file names with suffix (e.g., "_13_Lucene90_0.tim")
    auto segFileName = [&](const std::string& ext) -> std::string {
        if (suffix.empty()) return segment + "." + ext;
        return segment + "_" + suffix + "." + ext;
    };

    // Open .tim (term blocks)
    std::string timFile = segFileName("tim");
    termsIn_ = state.directory->openInput(timFile, store::IOContext::READ);
    version_ = CodecUtil::checkIndexHeader(*termsIn_, TERMS_CODEC_NAME,
                                            BLOCKTREE_VERSION_START, BLOCKTREE_VERSION_CURRENT,
                                            segID, suffix);

    // Open .tip (FST index)
    std::string tipFile = segFileName("tip");
    indexIn_ = state.directory->openInput(tipFile, store::IOContext::READ);
    CodecUtil::checkIndexHeader(*indexIn_, TERMS_INDEX_CODEC_NAME,
                                version_, version_, segID, suffix);

    // Read .tmd (metadata)
    std::string tmdFile = segFileName("tmd");
    auto metaIn = state.directory->openInput(tmdFile, store::IOContext::READ);
    CodecUtil::checkIndexHeader(*metaIn, TERMS_META_CODEC_NAME,
                                version_, version_, segID, suffix);

    // Let postingsReader read its sub-header from .tmd
    postingsReader_->init(*metaIn, state);

    // Read per-field metadata
    int numFields = metaIn->readVInt();
    if (numFields < 0) {
        throw CorruptIndexException("invalid numFields: " + std::to_string(numFields),
                                     metaIn->toString());
    }

    for (int i = 0; i < numFields; ++i) {
        FieldReaderMeta meta;
        meta.fieldNumber = metaIn->readVInt();
        meta.numTerms = metaIn->readVLong();
        if (meta.numTerms <= 0) {
            throw CorruptIndexException(
                "illegal numTerms for field " + std::to_string(meta.fieldNumber),
                metaIn->toString());
        }
        meta.rootCode = readBytesRef(*metaIn);

        // Look up field info
        const index::FieldInfo* fi = state.fieldInfos.fieldInfo(meta.fieldNumber);
        if (!fi) {
            throw CorruptIndexException(
                "invalid field number: " + std::to_string(meta.fieldNumber), metaIn->toString());
        }
        meta.fieldName = fi->name;
        meta.indexOptions = fi->indexOptions;

        meta.sumTotalTermFreq = metaIn->readVLong();
        // When frequencies are omitted, sumDocFreq == sumTotalTermFreq (single value)
        if (fi->indexOptions == index::IndexOptions::DOCS) {
            meta.sumDocFreq = meta.sumTotalTermFreq;
        } else {
            meta.sumDocFreq = metaIn->readVLong();
        }
        meta.docCount = metaIn->readVInt();
        meta.minTerm = readBytesRef(*metaIn);
        meta.maxTerm = readBytesRef(*metaIn);
        meta.indexStartFP = metaIn->readVLong();

        // Read FST metadata from .tmd (inline, per-field) and load FST bytes from .tip
        // In Lucene: var metadata = FST.readMetadata(metaIn, ByteSequenceOutputs.getSingleton());
        //            index = FST.fromFSTReader(metadata, new OffHeapFSTStore(indexIn, indexStartFP, metadata));
        std::unique_ptr<LuceneFST> fst;
        try {
            fst = std::make_unique<LuceneFST>(*metaIn, *indexIn_, meta.indexStartFP);
        } catch (const std::exception& e) {
            throw CorruptIndexException(
                "failed to read FST for field " + meta.fieldName + ": " + e.what(),
                metaIn->toString());
        }

        // Create FieldReader eagerly with the FST
        auto reader = std::make_shared<Lucene90FieldReader>(
            this, meta, std::move(fst), termsIn_.get());
        fieldReaders_[meta.fieldName] = reader;
        fieldMeta_[meta.fieldName] = std::move(meta);
    }

    // Read trailing lengths (big-endian) + footer
    // These are always present after the field loop
    try {
        /*int64_t indexLength =*/ metaIn->readLong();
        /*int64_t termsLength =*/ metaIn->readLong();
        CodecUtil::checkFooter(*metaIn);
    } catch (const std::exception&) {
        // Non-fatal: footer validation optional for backward compat
    }
}

Lucene90BlockTreeTermsReader::~Lucene90BlockTreeTermsReader() {
    try {
        close();
    } catch (...) {
    }
}

std::unique_ptr<index::Terms> Lucene90BlockTreeTermsReader::terms(const std::string& field) {
    auto readerIt = fieldReaders_.find(field);
    if (readerIt == fieldReaders_.end()) {
        return nullptr;
    }
    // Return the shared FieldReader (which implements Terms)
    // Wrap in a unique_ptr that does NOT own the object (shared ownership via shared_ptr)
    auto shared = readerIt->second;
    // Return a new wrapper that holds the shared_ptr
    class SharedTerms : public index::Terms {
    public:
        explicit SharedTerms(std::shared_ptr<Lucene90FieldReader> r) : reader_(std::move(r)) {}
        std::unique_ptr<index::TermsEnum> iterator() const override { return reader_->iterator(); }
        int64_t size() const override { return reader_->size(); }
        int getDocCount() const override { return reader_->getDocCount(); }
        int64_t getSumTotalTermFreq() const override { return reader_->getSumTotalTermFreq(); }
        int64_t getSumDocFreq() const override { return reader_->getSumDocFreq(); }
    private:
        std::shared_ptr<Lucene90FieldReader> reader_;
    };
    return std::make_unique<SharedTerms>(shared);
}

void Lucene90BlockTreeTermsReader::checkIntegrity() {
    // TODO: verify checksums
}

void Lucene90BlockTreeTermsReader::close() {
    fieldReaders_.clear();
    termsIn_.reset();
    indexIn_.reset();
    if (postingsReader_) {
        postingsReader_->close();
    }
}

// ==================== Lucene90FieldReader ====================

Lucene90FieldReader::Lucene90FieldReader(Lucene90BlockTreeTermsReader* parent,
                                           const FieldReaderMeta& meta,
                                           std::unique_ptr<LuceneFST> fst,
                                           store::IndexInput* timIn)
    : parent_(parent)
    , meta_(meta)
    , fst_(std::move(fst))
    , timIn_(timIn) {
    // Decode rootBlockFP from rootCode using MSB VLong
    if (!meta_.rootCode.empty()) {
        size_t pos = 0;
        int64_t code = LuceneFST::readMSBVLong(meta_.rootCode.data(), pos);
        rootBlockFP_ = code >> OUTPUT_FLAGS_NUM_BITS;
    }
}

std::unique_ptr<index::TermsEnum> Lucene90FieldReader::iterator() const {
    return std::make_unique<Lucene90SegmentTermsEnum>(this);
}

// ==================== Lucene90SegmentTermsEnum ====================

Lucene90SegmentTermsEnum::Lucene90SegmentTermsEnum(const Lucene90FieldReader* fieldReader)
    : fieldReader_(fieldReader) {
    // Clone .tim input for independent file pointer
    if (fieldReader_->timInput()) {
        timIn_ = fieldReader_->timInput()->clone();
    }
}

bool Lucene90SegmentTermsEnum::seekExact(const util::BytesRef& text) {
    // Convert BytesRef to vector for internal use
    std::vector<uint8_t> target(text.data(), text.data() + text.length());
    return seekExactInternal(target);
}

util::BytesRef Lucene90SegmentTermsEnum::term() const {
    if (!termFound_ || currentTerm_.empty()) {
        return util::BytesRef();
    }
    return util::BytesRef(currentTerm_.data(), currentTerm_.size());
}

std::unique_ptr<index::PostingsEnum> Lucene90SegmentTermsEnum::postings() {
    return postingsInternal(false);
}

std::unique_ptr<index::PostingsEnum> Lucene90SegmentTermsEnum::postingsWithPositions() {
    return postingsInternal(true);
}

std::unique_ptr<index::PostingsEnum> Lucene90SegmentTermsEnum::postingsInternal(bool needPositions) {
    if (!termFound_) return nullptr;
    auto* reader = fieldReader_->parent()->getPostingsReader();
    if (!reader) return nullptr;

    index::FieldInfo fieldInfo(fieldReader_->meta().fieldName, fieldReader_->meta().fieldNumber);
    fieldInfo.indexOptions = fieldReader_->meta().indexOptions;

    if (needPositions && frame_.termState.posStartFP >= 0) {
        return reader->postingsWithPositions(fieldInfo, frame_.termState);
    }
    return reader->postings(fieldInfo, frame_.termState);
}

bool Lucene90SegmentTermsEnum::seekExactInternal(const std::vector<uint8_t>& target) {
    termFound_ = false;
    currentTerm_.clear();
    frame_.reset();  // Full reset at the start of each seek

    const LuceneFST* fst = fieldReader_->fst();
    if (!fst) {
        frame_.fpOrig = fieldReader_->rootBlockFP();
        loadBlock(fieldReader_->rootBlockFP());
        frame_.prefixLength = 0;
        return scanToTerm(target);
    }

    // Walk FST to find the deepest matching block
    LuceneFST::Arc arc;
    fst->getFirstArc(arc);

    // Accumulate FST outputs along the path
    std::vector<uint8_t> accOutput;
    if (!arc.nextFinalOutput.empty()) {
        accOutput = arc.nextFinalOutput;
    }

    // Track the best (deepest) block code and its prefix depth
    // Start with root code (depth 0)
    std::vector<uint8_t> bestBlockCode = fieldReader_->meta().rootCode;
    int bestBlockDepth = 0;

    // Walk through the target term byte by byte
    for (size_t i = 0; i < target.size(); ++i) {
        int label = target[i] & 0xFF;

        LuceneFST::Arc nextArc;
        if (!fst->findTargetArc(label, arc, nextArc)) {
            break;
        }

        arc = nextArc;

        // Accumulate output
        if (!arc.output.empty()) {
            accOutput.insert(accOutput.end(), arc.output.begin(), arc.output.end());
        }

        // If this arc is final, it points to a block — update best
        if (arc.isFinal()) {
            std::vector<uint8_t> combined = accOutput;
            if (!arc.nextFinalOutput.empty()) {
                combined.insert(combined.end(), arc.nextFinalOutput.begin(),
                                arc.nextFinalOutput.end());
            }
            if (!combined.empty()) {
                bestBlockCode = combined;
                bestBlockDepth = static_cast<int>(i + 1);
            }
        }
    }

    // Decode the best block code → blockFP + flags + floor data position
    bool blockIsFloor = false, blockHasTerms = false;
    size_t posAfterCode = 0;
    int64_t blockFP = decodeBlockFP(bestBlockCode, blockIsFloor, blockHasTerms, &posAfterCode);

    // Set frame-level state before loadBlock
    frame_.fpOrig = blockFP;
    frame_.isFloor = blockIsFloor;
    frame_.hasTerms = blockHasTerms;
    frame_.prefixLength = bestBlockDepth;

    // Extract floor data if this is a floor block
    if (blockIsFloor && posAfterCode < bestBlockCode.size()) {
        frame_.floorData.assign(bestBlockCode.begin() + posAfterCode, bestBlockCode.end());
        frame_.floorDataPos = 0;

        // Read numFollowFloorBlocks (standard VInt) and first nextFloorLabel
        int pos = 0;
        frame_.numFollowFloorBlocks = readVInt(frame_.floorData.data(), pos);
        frame_.nextFloorLabel = frame_.floorData[pos++] & 0xFF;
        frame_.floorDataPos = pos;
    }

    // Load the initial block
    loadBlock(blockFP);

    // If it's a floor block, navigate to the right sub-block
    if (blockIsFloor && bestBlockDepth < static_cast<int>(target.size())) {
        scanToFloorFrame(target);
    }

    // Scan within the block for the exact term
    return scanToTerm(target);
}

int Lucene90SegmentTermsEnum::docFreq() const {
    return termFound_ ? frame_.termState.docFreq : 0;
}

int64_t Lucene90SegmentTermsEnum::totalTermFreq() const {
    return termFound_ ? frame_.termState.totalTermFreq : 0;
}


// ==================== Block Loading ====================

void Lucene90SegmentTermsEnum::loadBlock(int64_t blockFP) {
    if (!timIn_) return;

    // Clear only block-level state; preserve floor navigation state
    // (fpOrig, isFloor, floorData, floorDataPos, numFollowFloorBlocks, nextFloorLabel, prefixLength)
    frame_.fp = blockFP;
    // fpOrig is set by the caller — don't overwrite
    frame_.fpEnd = 0;
    frame_.entCount = 0;
    frame_.nextEnt = 0;
    frame_.isLastInFloor = false;
    frame_.isLeafBlock = false;
    // hasTerms is managed by floor navigation — don't reset
    frame_.suffixBytes.clear();
    frame_.suffixBytesPos = 0;
    frame_.suffixLengthBytes.clear();
    frame_.suffixLengthPos = 0;
    frame_.allSuffixesEqual = false;
    frame_.equalSuffixLength = 0;
    frame_.statBytes.clear();
    frame_.statPos = 0;
    frame_.statsSingletonRunLength = 0;
    frame_.metaBytes.clear();
    frame_.metaPos = 0;
    frame_.termState = Lucene90TermState{};

    timIn_->seek(blockFP);

    // 1. Entry count + isLastInFloor
    int code = timIn_->readVInt();
    frame_.entCount = static_cast<int>(static_cast<uint32_t>(code) >> 1);
    frame_.isLastInFloor = (code & 1) != 0;

    // 2. Suffix data
    int64_t codeL = timIn_->readVLong();
    frame_.isLeafBlock = (codeL & 0x04) != 0;
    int compressionAlg = static_cast<int>(codeL & 0x03);
    int numSuffixBytes = static_cast<int>(static_cast<uint64_t>(codeL) >> 3);

    // Compression algorithms: 0=NONE, 1=LOWERCASE_ASCII, 2=LZ4
    // numSuffixBytes is always the DECOMPRESSED size
    if (compressionAlg == 0) {
        // No compression — read raw bytes
        frame_.suffixBytes.resize(numSuffixBytes);
        if (numSuffixBytes > 0) {
            timIn_->readBytes(frame_.suffixBytes.data(), numSuffixBytes);
        }
    } else if (compressionAlg == 1) {
        // LOWERCASE_ASCII: 6-bit packing (4 chars → 3 bytes) + exceptions
        frame_.suffixBytes.resize(numSuffixBytes);
        decompressLowercaseAscii(*timIn_, frame_.suffixBytes.data(), numSuffixBytes);
    } else if (compressionAlg == 2) {
        // LZ4: streaming token-based decompression (reads from IndexInput directly)
        frame_.suffixBytes.resize(numSuffixBytes);
        decompressLZ4(*timIn_, frame_.suffixBytes.data(), numSuffixBytes);
    } else {
        throw CorruptIndexException(
            "unknown suffix compression: " + std::to_string(compressionAlg),
            timIn_->toString());
    }
    frame_.suffixBytesPos = 0;

    // 3. Suffix lengths
    int suffixLengthCode = timIn_->readVInt();
    frame_.allSuffixesEqual = (suffixLengthCode & 1) != 0;
    int numSuffixLengthBytes = static_cast<int>(static_cast<uint32_t>(suffixLengthCode) >> 1);

    if (frame_.allSuffixesEqual) {
        frame_.equalSuffixLength = timIn_->readByte() & 0xFF;
        frame_.suffixLengthBytes.clear();
    } else {
        frame_.suffixLengthBytes.resize(numSuffixLengthBytes);
        if (numSuffixLengthBytes > 0) {
            timIn_->readBytes(frame_.suffixLengthBytes.data(), numSuffixLengthBytes);
        }
    }
    frame_.suffixLengthPos = 0;

    // 4. Stats data
    int numStatBytes = timIn_->readVInt();
    frame_.statBytes.resize(numStatBytes);
    if (numStatBytes > 0) {
        timIn_->readBytes(frame_.statBytes.data(), numStatBytes);
    }
    frame_.statPos = 0;
    frame_.statsSingletonRunLength = 0;

    // 5. Metadata data
    int numMetaBytes = timIn_->readVInt();
    frame_.metaBytes.resize(numMetaBytes);
    if (numMetaBytes > 0) {
        timIn_->readBytes(frame_.metaBytes.data(), numMetaBytes);
    }
    frame_.metaPos = 0;

    frame_.fpEnd = timIn_->getFilePointer();
    frame_.nextEnt = 0;
}

// ==================== Term Scanning ====================

bool Lucene90SegmentTermsEnum::scanToTerm(const std::vector<uint8_t>& target) {
    int prefixLen = frame_.prefixLength;

    // Scan through all entries in the block
    for (int i = 0; i < frame_.entCount; ++i) {
        frame_.nextEnt = i;

        // Get suffix length for this entry
        int suffixLen;
        bool isSubBlock = false;
        int64_t subBlockFP = -1;

        if (frame_.allSuffixesEqual) {
            suffixLen = frame_.equalSuffixLength;
        } else {
            // Read VInt from suffixLengthBytes
            int pos = frame_.suffixLengthPos;
            int lenCode = readVInt(frame_.suffixLengthBytes.data(), pos);
            frame_.suffixLengthPos = pos;

            if (!frame_.isLeafBlock) {
                // Non-leaf: low bit indicates sub-block
                suffixLen = static_cast<int>(static_cast<uint32_t>(lenCode) >> 1);
                isSubBlock = (lenCode & 1) != 0;
                if (isSubBlock) {
                    // Read sub-block relative FP from suffixLengthBytes
                    pos = frame_.suffixLengthPos;
                    int64_t subCode = readVLong(frame_.suffixLengthBytes.data(), pos);
                    frame_.suffixLengthPos = pos;
                    subBlockFP = frame_.fp - subCode;
                }
            } else {
                suffixLen = lenCode;
            }
        }

        // Read suffix bytes for this entry
        const uint8_t* suffixData = frame_.suffixBytes.data() + frame_.suffixBytesPos;
        frame_.suffixBytesPos += suffixLen;

        int targetRemaining = static_cast<int>(target.size()) - prefixLen;

        if (isSubBlock) {
            // Sub-block entry: check if target could be in this sub-block.
            // The sub-block's full prefix is target[0:prefixLen] + suffix.
            // Descend if the target starts with that prefix.
            if (targetRemaining >= suffixLen && suffixLen > 0) {
                bool prefixMatch = true;
                for (int j = 0; j < suffixLen; ++j) {
                    if ((suffixData[j] & 0xFF) != (target[prefixLen + j] & 0xFF)) {
                        // Check if we've gone past the target (entries are sorted)
                        if ((suffixData[j] & 0xFF) > (target[prefixLen + j] & 0xFF)) {
                            // Past the target — not in this block or any sub-block
                            termFound_ = false;
                            return false;
                        }
                        prefixMatch = false;
                        break;
                    }
                }
                if (prefixMatch) {
                    // Target starts with this sub-block's prefix — descend
                    int newPrefixLen = prefixLen + suffixLen;

                    // Set up frame for the sub-block (non-floor, fresh block)
                    frame_.fpOrig = subBlockFP;
                    frame_.isFloor = false;
                    frame_.hasTerms = true;
                    frame_.floorData.clear();
                    frame_.numFollowFloorBlocks = 0;
                    frame_.nextFloorLabel = 0;
                    frame_.prefixLength = newPrefixLen;

                    loadBlock(subBlockFP);
                    return scanToTerm(target);  // Recurse into sub-block
                }
            }
            // Sub-block doesn't match — skip (no stats/meta to consume)
            continue;
        }

        // Term entry — compare suffix with target[prefixLen:]
        if (prefixLen > static_cast<int>(target.size())) {
            // Prefix is longer than target — can't match
            decodeMetaData();
            continue;
        }

        int targetSuffixLen = targetRemaining;
        int cmp = 0;
        int minLen = std::min(suffixLen, targetSuffixLen);
        for (int j = 0; j < minLen; ++j) {
            int a = suffixData[j] & 0xFF;
            int b = target[prefixLen + j] & 0xFF;
            if (a != b) {
                cmp = a - b;
                break;
            }
        }
        if (cmp == 0) {
            cmp = suffixLen - targetSuffixLen;
        }

        if (cmp == 0) {
            // Exact match! Decode metadata.
            decodeMetaData();
            termFound_ = true;
            return true;
        }

        if (cmp > 0) {
            // Past the target in sorted order — term doesn't exist
            termFound_ = false;
            return false;
        }

        // cmp < 0: haven't reached the target yet, skip stats + meta
        decodeMetaData();
    }

    termFound_ = false;
    return false;
}

void Lucene90SegmentTermsEnum::decodeMetaData() {
    // Decode stats from statBytes
    if (frame_.statsSingletonRunLength > 0) {
        frame_.termState.docFreq = 1;
        frame_.termState.totalTermFreq = 1;
        frame_.statsSingletonRunLength--;
    } else if (frame_.statPos < static_cast<int>(frame_.statBytes.size())) {
        int pos = frame_.statPos;
        int token = readVInt(frame_.statBytes.data(), pos);
        if ((token & 1) == 1) {
            // Singleton run
            frame_.termState.docFreq = 1;
            frame_.termState.totalTermFreq = 1;
            frame_.statsSingletonRunLength = static_cast<int>(static_cast<uint32_t>(token) >> 1);
        } else {
            frame_.termState.docFreq = static_cast<int>(static_cast<uint32_t>(token) >> 1);
            bool hasFreqs = fieldReader_->meta().indexOptions >= index::IndexOptions::DOCS_AND_FREQS;
            if (!hasFreqs || fieldReader_->meta().indexOptions == index::IndexOptions::DOCS) {
                frame_.termState.totalTermFreq = frame_.termState.docFreq;
            } else {
                frame_.termState.totalTermFreq =
                    frame_.termState.docFreq + readVLong(frame_.statBytes.data(), pos);
            }
        }
        frame_.statPos = pos;
    }

    // Decode metadata from metaBytes (postings FPs)
    bool absolute = (frame_.nextEnt == 0);
    if (frame_.metaPos < static_cast<int>(frame_.metaBytes.size())) {
        int pos = frame_.metaPos;
        const uint8_t* data = frame_.metaBytes.data();

        bool fieldHasPositions = fieldReader_->meta().indexOptions
                                 >= index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
        bool fieldHasOffsets = fieldReader_->meta().indexOptions
                               >= index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS_AND_OFFSETS;
        bool fieldHasPayloads = false;  // Not tracked in MVP

        if (absolute) {
            frame_.termState.docStartFP = 0;
            frame_.termState.posStartFP = 0;
            frame_.termState.payStartFP = 0;
        }

        int64_t l = readVLong(data, pos);
        if ((l & 0x01) == 0) {
            frame_.termState.docStartFP += l >> 1;
            if (frame_.termState.docFreq == 1) {
                frame_.termState.singletonDocID = readVInt(data, pos);
            } else {
                frame_.termState.singletonDocID = -1;
            }
        } else {
            // Delta singleton
            // assert !absolute
            int64_t delta = l >> 1;
            // Zig-zag decode
            frame_.termState.singletonDocID +=
                static_cast<int>((delta >> 1) ^ -(delta & 1));
        }

        if (fieldHasPositions) {
            frame_.termState.posStartFP += readVLong(data, pos);
            if (fieldHasOffsets || fieldHasPayloads) {
                frame_.termState.payStartFP += readVLong(data, pos);
            }
            if (frame_.termState.totalTermFreq > 128) {
                frame_.termState.lastPosBlockOffset = readVLong(data, pos);
            } else {
                frame_.termState.lastPosBlockOffset = -1;
            }
        }

        if (frame_.termState.docFreq > 128) {
            frame_.termState.skipOffset = readVLong(data, pos);
        } else {
            frame_.termState.skipOffset = -1;
        }

        frame_.metaPos = pos;
    }
}

// ==================== Floor Block Navigation ====================

void Lucene90SegmentTermsEnum::scanToFloorFrame(const std::vector<uint8_t>& target) {
    if (!frame_.isFloor || frame_.prefixLength >= static_cast<int>(target.size())) {
        return;
    }

    int targetLabel = target[frame_.prefixLength] & 0xFF;

    // If target label is before the first floor block's label, we're already in the right block
    if (targetLabel < frame_.nextFloorLabel) {
        return;
    }

    // Scan through floor blocks to find the right one
    // Each floor entry: VLong((deltaFP << 1) | hasTerms), then byte(nextLabel) if not last
    int64_t newFP;
    while (true) {
        int pos = frame_.floorDataPos;
        int64_t code = readVLong(frame_.floorData.data(), pos);
        newFP = frame_.fpOrig + (code >> 1);
        frame_.hasTerms = (code & 1) != 0;
        frame_.floorDataPos = pos;

        frame_.numFollowFloorBlocks--;

        if (frame_.numFollowFloorBlocks != 0) {
            int nextLabel = frame_.floorData[frame_.floorDataPos++] & 0xFF;
            if (targetLabel < nextLabel) {
                frame_.nextFloorLabel = nextLabel;
                break;
            }
            frame_.nextFloorLabel = nextLabel;
        } else {
            frame_.nextFloorLabel = 256;
            break;
        }
    }

    // Load the new sub-block (loadBlock preserves frame-level floor state)
    loadBlock(newFP);
}

// ==================== FST Output Decoding ====================

int64_t Lucene90SegmentTermsEnum::decodeBlockFP(const std::vector<uint8_t>& output,
                                                   bool& isFloor, bool& hasTerms,
                                                   size_t* posAfter) const {
    if (output.empty()) return 0;

    int64_t code;
    size_t endPos = 0;
    if (fieldReader_->parent()->version() >= BLOCKTREE_VERSION_MSB_VLONG) {
        size_t pos = 0;
        code = LuceneFST::readMSBVLong(output.data(), pos);
        endPos = pos;
    } else {
        int pos = 0;
        code = readVLong(output.data(), pos);
        endPos = static_cast<size_t>(pos);
    }

    isFloor = (code & OUTPUT_FLAG_IS_FLOOR) != 0;
    hasTerms = (code & OUTPUT_FLAG_HAS_TERMS) != 0;
    if (posAfter) *posAfter = endPos;
    return code >> OUTPUT_FLAGS_NUM_BITS;
}

// ==================== VInt/VLong from byte array ====================

int32_t Lucene90SegmentTermsEnum::readVInt(const uint8_t* data, int& pos) {
    uint8_t b = data[pos++];
    if ((b & 0x80) == 0) return b;
    int32_t i = b & 0x7F;
    b = data[pos++];
    i |= (b & 0x7F) << 7;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (b & 0x7F) << 14;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (b & 0x7F) << 21;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (b & 0x0F) << 28;
    return i;
}

int64_t Lucene90SegmentTermsEnum::readVLong(const uint8_t* data, int& pos) {
    uint8_t b = data[pos++];
    if ((b & 0x80) == 0) return b;
    int64_t i = b & 0x7FL;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 7;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 14;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 21;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 28;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 35;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 42;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x7FL) << 49;
    if ((b & 0x80) == 0) return i;
    b = data[pos++];
    i |= (static_cast<int64_t>(b) & 0x01L) << 56;
    return i;
}

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
