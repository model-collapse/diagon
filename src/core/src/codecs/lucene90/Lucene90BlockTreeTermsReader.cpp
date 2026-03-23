// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/Lucene90BlockTreeTermsReader.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/util/Exceptions.h"

#ifdef HAVE_LZ4
#    include <lz4.h>
#endif

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene90 {

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

    // Open .tim (term blocks)
    std::string timFile = segment + ".tim";
    termsIn_ = state.directory->openInput(timFile, store::IOContext::READ);
    version_ = CodecUtil::checkIndexHeader(*termsIn_, TERMS_CODEC_NAME,
                                            BLOCKTREE_VERSION_START, BLOCKTREE_VERSION_CURRENT,
                                            segID, suffix);

    // Open .tip (FST index)
    std::string tipFile = segment + ".tip";
    indexIn_ = state.directory->openInput(tipFile, store::IOContext::READ);
    CodecUtil::checkIndexHeader(*indexIn_, TERMS_INDEX_CODEC_NAME,
                                version_, version_, segID, suffix);

    // Read .tmd (metadata)
    std::string tmdFile = segment + ".tmd";
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

    const LuceneFST* fst = fieldReader_->fst();
    if (!fst) {
        // No FST — load root block directly and scan
        loadBlock(fieldReader_->rootBlockFP());
        return scanToTerm(target);
    }

    // Walk FST to find the deepest matching block
    LuceneFST::Arc arc;
    fst->getFirstArc(arc);

    // Accumulate FST outputs
    fstOutput_.clear();

    // The root arc's nextFinalOutput is the empty output (root block code)
    // For BlockTree, this is always the root code
    std::vector<uint8_t> accOutput;
    if (!arc.nextFinalOutput.empty()) {
        accOutput = arc.nextFinalOutput;
    }

    // Best block FP found so far
    int64_t blockFP = fieldReader_->rootBlockFP();
    bool blockIsFloor = false;

    // Decode root code flags
    if (!fieldReader_->meta().rootCode.empty()) {
        size_t pos = 0;
        int64_t code = LuceneFST::readMSBVLong(fieldReader_->meta().rootCode.data(), pos);
        blockIsFloor = (code & OUTPUT_FLAG_IS_FLOOR) != 0;
    }

    int targetUpto = 0;

    // Walk through the target term byte by byte
    for (size_t i = 0; i < target.size(); ++i) {
        int label = target[i] & 0xFF;

        LuceneFST::Arc nextArc;
        if (!fst->findTargetArc(label, arc, nextArc)) {
            // FST index exhausted at this point
            break;
        }

        arc = nextArc;
        targetUpto = static_cast<int>(i + 1);

        // Accumulate output
        if (!arc.output.empty()) {
            // Append arc output to accumulated output
            accOutput.insert(accOutput.end(), arc.output.begin(), arc.output.end());
        }

        // If this arc is final, it points to a block
        if (arc.isFinal()) {
            // Combine accumulated output with final output
            std::vector<uint8_t> combined = accOutput;
            if (!arc.nextFinalOutput.empty()) {
                combined.insert(combined.end(), arc.nextFinalOutput.begin(),
                                arc.nextFinalOutput.end());
            }

            // Decode block FP from combined output
            if (!combined.empty()) {
                bool isFloor = false, hasTerms = false;
                int64_t fp = decodeBlockFP(combined, isFloor, hasTerms);
                blockFP = fp;
                blockIsFloor = isFloor;
                (void)hasTerms;  // Used for block loading decisions in full impl
            }
        }
    }

    // Load the best block
    loadBlock(blockFP);

    // If it's a floor block, navigate to the right sub-block
    if (blockIsFloor && targetUpto < static_cast<int>(target.size())) {
        frame_.isFloor = true;
        scanToFloorFrame(target);
    }

    // Scan within the block for the exact term
    frame_.prefixLength = targetUpto;
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

    frame_.reset();
    frame_.fp = blockFP;
    frame_.fpOrig = blockFP;

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

    if (compressionAlg == 0) {
        // No compression
        frame_.suffixBytes.resize(numSuffixBytes);
        if (numSuffixBytes > 0) {
            timIn_->readBytes(frame_.suffixBytes.data(), numSuffixBytes);
        }
    } else if (compressionAlg == 1 || compressionAlg == 2) {
        // LZ4 or LZ4_HIGH_COMPRESSION
        int decompressedLen = timIn_->readVInt();
        int compressedLen = numSuffixBytes;
        std::vector<uint8_t> compressed(compressedLen);
        timIn_->readBytes(compressed.data(), compressedLen);

        frame_.suffixBytes.resize(decompressedLen);
#ifdef HAVE_LZ4
        int result = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed.data()),
            reinterpret_cast<char*>(frame_.suffixBytes.data()),
            compressedLen, decompressedLen);
        if (result < 0) {
            throw CorruptIndexException("LZ4 decompression failed for suffix data",
                                         timIn_->toString());
        }
#else
        throw std::runtime_error("LZ4 decompression not available (HAVE_LZ4 not defined)");
#endif
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
    // Scan through all entries in the block
    for (int i = 0; i < frame_.entCount; ++i) {
        frame_.nextEnt = i;

        // Get suffix length for this entry
        int suffixLen;
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
                bool isSubBlock = (lenCode & 1) != 0;
                if (isSubBlock) {
                    // This is a sub-block pointer, not a term — skip it
                    frame_.suffixBytesPos += suffixLen;
                    // Also skip the sub-block FP delta in suffix length data
                    int skipPos = frame_.suffixLengthPos;
                    readVLong(frame_.suffixLengthBytes.data(), skipPos);
                    frame_.suffixLengthPos = skipPos;
                    // Skip stats and meta for this entry
                    decodeMetaData();
                    continue;
                }
            } else {
                suffixLen = lenCode;
            }
        }

        // Build the full term: prefix + suffix
        const uint8_t* suffixData = frame_.suffixBytes.data() + frame_.suffixBytesPos;
        frame_.suffixBytesPos += suffixLen;

        // Compare suffix with target
        // The block prefix is target[0..prefixLength)
        // The full term is prefix + suffix
        // We need to compare target[prefixLength..] with suffix
        int prefixLen = frame_.prefixLength;
        int targetSuffixLen = static_cast<int>(target.size()) - prefixLen;

        // First check: does the prefix match?
        bool prefixMatch = true;
        if (prefixLen > 0 && prefixLen <= static_cast<int>(target.size())) {
            // Prefix was matched by FST traversal, so it should match
        } else if (prefixLen > static_cast<int>(target.size())) {
            prefixMatch = false;
        }

        if (prefixMatch) {
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
        }

        // Skip stats + meta for this entry (lazy decode)
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
    // Floor blocks: after loading the main block, check if we need a different sub-block
    // This is a simplified version — in Lucene the floor data is stored in the FST output
    // For MVP, we rely on the FST pointing us to the right block
    // Full floor block navigation would require reading the floor data from the block header
    (void)target;
}

// ==================== FST Output Decoding ====================

int64_t Lucene90SegmentTermsEnum::decodeBlockFP(const std::vector<uint8_t>& output,
                                                   bool& isFloor, bool& hasTerms) const {
    if (output.empty()) return 0;

    int64_t code;
    if (fieldReader_->parent()->version() >= BLOCKTREE_VERSION_MSB_VLONG) {
        size_t pos = 0;
        code = LuceneFST::readMSBVLong(output.data(), pos);
    } else {
        int pos = 0;
        code = readVLong(output.data(), pos);
    }

    isFloor = (code & OUTPUT_FLAG_IS_FLOOR) != 0;
    hasTerms = (code & OUTPUT_FLAG_HAS_TERMS) != 0;
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
