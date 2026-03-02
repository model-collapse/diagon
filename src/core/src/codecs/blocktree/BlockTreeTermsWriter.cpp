// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"

#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>

#ifdef HAVE_LZ4
#include <lz4.h>
#endif

namespace diagon {
namespace codecs {
namespace blocktree {

namespace {

// Buffer encoding helpers for column-stride section building.
// These mirror IndexOutput::writeVInt/writeVLong but append to a byte vector.

void bufEncodeVInt(std::vector<uint8_t>& buf, int32_t val) {
    auto v = static_cast<uint32_t>(val);
    while (v > 0x7F) {
        buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(v));
}

void bufEncodeVLong(std::vector<uint8_t>& buf, int64_t val) {
    auto v = static_cast<uint64_t>(val);
    while (v > 0x7F) {
        buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(v));
}

}  // anonymous namespace

BlockTreeTermsWriter::BlockTreeTermsWriter(store::IndexOutput* timOut, store::IndexOutput* tipOut,
                                           const index::FieldInfo& fieldInfo, const Config& config)
    : timOut_(timOut)
    , tipOut_(tipOut)
    , fieldInfo_(fieldInfo)
    , config_(config)
    , numTerms_(0)
    , termsStartFP_(timOut->getFilePointer())  // Capture starting FP for this field
    , finished_(false)
    , sumTotalTermFreq_(0)
    , sumDocFreq_(0)
    , docCount_(0) {
    if (!timOut_ || !tipOut_) {
        throw std::invalid_argument("Output streams cannot be null");
    }

    if (config_.minItemsInBlock < 2) {
        throw std::invalid_argument("minItemsInBlock must be >= 2");
    }

    if (config_.maxItemsInBlock < config_.minItemsInBlock) {
        throw std::invalid_argument("maxItemsInBlock must be >= minItemsInBlock");
    }
}

void BlockTreeTermsWriter::addTerm(const util::BytesRef& term, const TermStats& stats) {
    if (finished_) {
        throw std::runtime_error("Writer already finished");
    }

    // Verify sorted order
    if (lastTerm_.length() > 0 && term <= lastTerm_) {
        throw std::invalid_argument("Terms must be added in sorted order");
    }

    // Add to pending terms
    pendingTerms_.emplace_back(term, stats);

    // Save last term
    lastTermData_.assign(term.data(), term.data() + term.length());
    lastTerm_ = util::BytesRef(lastTermData_.data(), lastTermData_.size());
    numTerms_++;

    // Accumulate field-level statistics
    sumTotalTermFreq_ += stats.totalTermFreq;
    sumDocFreq_ += stats.docFreq;

    // Flush block if we hit max size
    if (static_cast<int>(pendingTerms_.size()) >= config_.maxItemsInBlock) {
        writeBlock();
    }
}

void BlockTreeTermsWriter::finish() {
    if (finished_) {
        return;
    }

    // Write remaining pending terms
    if (!pendingTerms_.empty()) {
        writeBlock();
    }

    // Write compact block index to .tip file
    writeBlockIndex();

    finished_ = true;
}

void BlockTreeTermsWriter::writeBlock() {
    if (pendingTerms_.empty()) {
        return;
    }

    int64_t blockFP = timOut_->getFilePointer();

    // Compute common prefix for all terms in block
    util::BytesRef prefix = pendingTerms_[0].term;
    int prefixLen = static_cast<int>(prefix.length());

    for (size_t i = 1; i < pendingTerms_.size(); i++) {
        prefixLen = sharedPrefixLength(prefix, pendingTerms_[i].term);
        if (prefixLen == 0) {
            break;
        }
    }

    // Write block header: VInt(code) where code = (termCount << 1) | isLastInFloor
    // Prefix is NOT stored here — derived from .tip block index first term at read time
    int termCount = static_cast<int>(pendingTerms_.size());
    timOut_->writeVInt((termCount << 1) | 0);  // isLastInFloor = 0 (no floor blocks)

    // === Section 1: Suffix data (with optional LZ4 compression) ===
    // Format: [suffix lengths section] + [concatenated suffix bytes]
    // Suffix lengths section uses all-equal optimization when possible.

    // Collect suffix lengths and check for all-equal
    std::vector<uint8_t> suffixLengths;
    suffixLengths.reserve(termCount);
    bool allSuffixEqual = true;
    int firstSuffixLen = -1;

    for (const auto& pending : pendingTerms_) {
        int suffixLen = static_cast<int>(pending.term.length()) - prefixLen;
        suffixLengths.push_back(static_cast<uint8_t>(suffixLen));
        if (firstSuffixLen < 0) {
            firstSuffixLen = suffixLen;
        } else if (suffixLen != firstSuffixLen) {
            allSuffixEqual = false;
        }
    }

    std::vector<uint8_t> suffixBuf;
    suffixBuf.reserve(termCount * 10);

    // Encode suffix lengths section
    if (allSuffixEqual && !suffixLengths.empty()) {
        // All suffix lengths equal: VInt((1 << 1) | 1) + byte(commonLength)
        bufEncodeVInt(suffixBuf, (1 << 1) | 1);
        suffixBuf.push_back(static_cast<uint8_t>(firstSuffixLen));
    } else {
        // Individual lengths: VInt((numLengths << 1) | 0) + [numLengths bytes]
        bufEncodeVInt(suffixBuf, (static_cast<int>(suffixLengths.size()) << 1) | 0);
        suffixBuf.insert(suffixBuf.end(), suffixLengths.begin(), suffixLengths.end());
    }

    // Concatenated suffix bytes (no per-term VInt overhead)
    for (size_t i = 0; i < pendingTerms_.size(); i++) {
        int suffixLen = suffixLengths[i];
        if (suffixLen > 0) {
            const uint8_t* suffixStart = pendingTerms_[i].term.data() + prefixLen;
            suffixBuf.insert(suffixBuf.end(), suffixStart, suffixStart + suffixLen);
        }
    }

    // Write suffix section: VLong((uncompressedSize << 3) | flags)
    // Bit 0: LZ4 compressed
    bool compressed = false;
#ifdef HAVE_LZ4
    if (suffixBuf.size() >= 32) {
        int srcSize = static_cast<int>(suffixBuf.size());
        int maxDstSize = LZ4_compressBound(srcSize);
        std::vector<uint8_t> compBuf(maxDstSize);
        int compSize = LZ4_compress_default(
            reinterpret_cast<const char*>(suffixBuf.data()),
            reinterpret_cast<char*>(compBuf.data()),
            srcSize, maxDstSize);
        if (compSize > 0 && static_cast<size_t>(compSize) < suffixBuf.size() * 3 / 4) {
            // Compressed saves >25%
            timOut_->writeVLong(static_cast<int64_t>((suffixBuf.size() << 3) | 0x01));
            timOut_->writeVInt(compSize);
            timOut_->writeBytes(compBuf.data(), compSize);
            compressed = true;
        }
    }
#endif
    if (!compressed) {
        timOut_->writeVLong(static_cast<int64_t>((suffixBuf.size() << 3) | 0x00));
        timOut_->writeBytes(suffixBuf.data(), suffixBuf.size());
    }

    // === Section 2: Stats (column-stride with singleton RLE) ===
    // Singleton RLE: consecutive terms with docFreq=1 AND totalTermFreq=1
    // are encoded as VInt(((runCount-1) << 1) | 1).
    // Non-singleton: VInt((docFreq << 1) | 0) + VLong(totalTermFreq - docFreq).
    std::vector<uint8_t> statsBuf;
    statsBuf.reserve(pendingTerms_.size() * 3);
    {
        size_t i = 0;
        size_t count = pendingTerms_.size();
        while (i < count) {
            if (pendingTerms_[i].stats.docFreq == 1 &&
                pendingTerms_[i].stats.totalTermFreq == 1) {
                // Count consecutive singletons
                size_t runStart = i;
                while (i < count &&
                       pendingTerms_[i].stats.docFreq == 1 &&
                       pendingTerms_[i].stats.totalTermFreq == 1) {
                    i++;
                }
                int runCount = static_cast<int>(i - runStart);
                bufEncodeVInt(statsBuf, ((runCount - 1) << 1) | 1);
            } else {
                bufEncodeVInt(statsBuf, (pendingTerms_[i].stats.docFreq << 1) | 0);
                bufEncodeVLong(statsBuf,
                    pendingTerms_[i].stats.totalTermFreq - pendingTerms_[i].stats.docFreq);
                i++;
            }
        }
    }
    timOut_->writeVInt(static_cast<int>(statsBuf.size()));
    timOut_->writeBytes(statsBuf.data(), statsBuf.size());

    // === Section 3: Metadata (conditional column-stride file pointer deltas) ===
    // Format: flags byte + [postingsFP column] + [posStartFP column?] + [skipStartFP section?]
    // flags bit0: always 1 (postingsFP present)
    // flags bit1: posStartFP column present (only if any term has posStartFP >= 0)
    // flags bit2: skipStartFP section present (only if any term has skipStartFP >= 0)
    std::vector<uint8_t> metaBuf;
    metaBuf.reserve(pendingTerms_.size() * 9);
    {
        bool blockHasSkip = false, blockHasPos = false;
        for (const auto& pending : pendingTerms_) {
            if (pending.stats.skipStartFP >= 0) blockHasSkip = true;
            if (pending.stats.posStartFP >= 0) blockHasPos = true;
        }

        // Write flags byte
        uint8_t flags = 0x01;  // bit0: postingsFP always present
        if (blockHasPos)  flags |= 0x02;
        if (blockHasSkip) flags |= 0x04;
        metaBuf.push_back(flags);

        // Column 1: postingsFP (always present)
        int64_t lastFP = 0;
        for (const auto& pending : pendingTerms_) {
            bufEncodeVLong(metaBuf, pending.stats.postingsFP - lastFP);
            lastFP = pending.stats.postingsFP;
        }

        // Column 2: posStartFP (only if any term has position data)
        if (blockHasPos) {
            lastFP = 0;
            for (const auto& pending : pendingTerms_) {
                bufEncodeVLong(metaBuf, pending.stats.posStartFP - lastFP);
                lastFP = pending.stats.posStartFP;
            }
        }

        // Column 3: skipStartFP (sparse — bitmap + values for terms with skip data)
        if (blockHasSkip) {
            // Write bitmap: 1 bit per term, ceil(termCount/8) bytes
            int bitmapBytes = (termCount + 7) / 8;
            size_t bitmapStart = metaBuf.size();
            metaBuf.resize(metaBuf.size() + bitmapBytes, 0);
            for (int i = 0; i < termCount; i++) {
                if (pendingTerms_[i].stats.skipStartFP >= 0) {
                    metaBuf[bitmapStart + (i / 8)] |= (1 << (i % 8));
                }
            }
            // Delta-encode only the valid skipStartFP values
            lastFP = 0;
            for (int i = 0; i < termCount; i++) {
                if (pendingTerms_[i].stats.skipStartFP >= 0) {
                    bufEncodeVLong(metaBuf, pendingTerms_[i].stats.skipStartFP - lastFP);
                    lastFP = pendingTerms_[i].stats.skipStartFP;
                }
            }
        }
    }
    timOut_->writeVInt(static_cast<int>(metaBuf.size()));
    timOut_->writeBytes(metaBuf.data(), metaBuf.size());

    // Record block entry for compact .tip index
    const auto& firstTerm = pendingTerms_[0].term;
    blockEntries_.emplace_back(firstTerm.data(), firstTerm.length(), blockFP);

    // Clear pending terms
    pendingTerms_.clear();
}

void BlockTreeTermsWriter::writeBlockIndex() {
    // Write compact block index to .tip file
    // TIP3 format: flat block list (no FST trie overhead)
    // Saves ~200 KB for Reuters vs TIP2 (which serialized the full packed FST trie)

    tipOut_->writeInt(0x54495033);          // "TIP3" magic (compact block list)
    tipOut_->writeString(fieldInfo_.name);  // Field name
    tipOut_->writeVLong(termsStartFP_);     // Starting file pointer for this field's terms
    tipOut_->writeVLong(numTerms_);

    // Write block entries
    tipOut_->writeVInt(static_cast<int>(blockEntries_.size()));
    for (const auto& entry : blockEntries_) {
        tipOut_->writeVInt(static_cast<int>(entry.termData.size()));
        tipOut_->writeBytes(entry.termData.data(), entry.termData.size());
        tipOut_->writeVLong(entry.blockFP);
    }
}

int BlockTreeTermsWriter::sharedPrefixLength(const util::BytesRef& a,
                                             const util::BytesRef& b) const {
    size_t minLen = std::min(a.length(), b.length());
    int shared = 0;

    for (size_t i = 0; i < minLen; i++) {
        if (a[i] != b[i]) {
            break;
        }
        shared++;
    }

    return shared;
}

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
