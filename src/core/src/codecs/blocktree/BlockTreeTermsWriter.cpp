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
    // DEBUG: Show what file pointer we captured

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

    // Write block header: [prefixLen][prefix bytes][termCount]
    timOut_->writeVInt(prefixLen);
    if (prefixLen > 0) {
        timOut_->writeBytes(prefix.data(), prefixLen);
    }
    timOut_->writeVInt(static_cast<int>(pendingTerms_.size()));

    // === Section 1: Suffix data (with optional LZ4 compression) ===
    // Collect all suffix lengths + suffix bytes into a flat buffer
    std::vector<uint8_t> suffixBuf;
    suffixBuf.reserve(pendingTerms_.size() * 10);
    for (const auto& pending : pendingTerms_) {
        int suffixLen = static_cast<int>(pending.term.length()) - prefixLen;
        bufEncodeVInt(suffixBuf, suffixLen);
        if (suffixLen > 0) {
            const uint8_t* suffixStart = pending.term.data() + prefixLen;
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

    // === Section 3: Metadata (column-stride file pointer deltas) ===
    // All postingsFP deltas, then all posStartFP deltas, then all skipStartFP deltas.
    // Each column uses cumulative delta encoding (reset to 0 at block start).
    std::vector<uint8_t> metaBuf;
    metaBuf.reserve(pendingTerms_.size() * 9);
    {
        int64_t lastFP = 0;
        for (const auto& pending : pendingTerms_) {
            bufEncodeVLong(metaBuf, pending.stats.postingsFP - lastFP);
            lastFP = pending.stats.postingsFP;
        }
        lastFP = 0;
        for (const auto& pending : pendingTerms_) {
            bufEncodeVLong(metaBuf, pending.stats.posStartFP - lastFP);
            lastFP = pending.stats.posStartFP;
        }
        lastFP = 0;
        for (const auto& pending : pendingTerms_) {
            bufEncodeVLong(metaBuf, pending.stats.skipStartFP - lastFP);
            lastFP = pending.stats.skipStartFP;
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
