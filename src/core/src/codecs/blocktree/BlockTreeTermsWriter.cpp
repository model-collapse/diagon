// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"

#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace blocktree {

BlockTreeTermsWriter::BlockTreeTermsWriter(store::IndexOutput* timOut, store::IndexOutput* tipOut,
                                           const index::FieldInfo& fieldInfo, const Config& config)
    : timOut_(timOut)
    , tipOut_(tipOut)
    , fieldInfo_(fieldInfo)
    , config_(config)
    , numTerms_(0)
    , termsStartFP_(timOut->getFilePointer())  // Capture starting FP for this field
    , finished_(false) {
    // DEBUG: Show what file pointer we captured
    std::cerr << "[BlockTreeTermsWriter] Created for field '" << fieldInfo_.name
              << "', termsStartFP=" << termsStartFP_ << std::endl;

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

    // DEBUG: Show where we ended up
    int64_t endFP = timOut_->getFilePointer();
    std::cerr << "[BlockTreeTermsWriter] Finishing field '" << fieldInfo_.name
              << "', endFP=" << endFP << ", bytesWritten=" << (endFP - termsStartFP_)
              << ", numTerms=" << numTerms_ << std::endl;

    // Write FST index to .tip file
    writeFST();

    finished_ = true;
}

void BlockTreeTermsWriter::writeBlock() {
    if (pendingTerms_.empty()) {
        return;
    }

    // Record block file pointer
    int64_t blockFP = timOut_->getFilePointer();

    // DEBUG: Show first term being written
    if (!pendingTerms_.empty()) {
        const auto& firstTerm = pendingTerms_[0].term;
        std::string firstTermStr(reinterpret_cast<const char*>(firstTerm.data()), firstTerm.length());
        std::cerr << "[BlockTreeTermsWriter::writeBlock] Writing block for field '" << fieldInfo_.name
                  << "' at FP=" << blockFP << ", first term='" << firstTermStr
                  << "', num terms=" << pendingTerms_.size() << std::endl;
    }

    // Compute common prefix for all terms in block
    util::BytesRef prefix = pendingTerms_[0].term;
    int prefixLen = static_cast<int>(prefix.length());

    for (size_t i = 1; i < pendingTerms_.size(); i++) {
        prefixLen = sharedPrefixLength(prefix, pendingTerms_[i].term);
        if (prefixLen == 0) {
            break;
        }
    }

    // Write block header
    // Format: [prefixLen][prefix bytes][termCount]
    timOut_->writeVInt(prefixLen);
    if (prefixLen > 0) {
        timOut_->writeBytes(prefix.data(), prefixLen);
    }
    timOut_->writeVInt(static_cast<int>(pendingTerms_.size()));

    // Write each term's suffix and stats
    for (const auto& pending : pendingTerms_) {
        const util::BytesRef& term = pending.term;
        const TermStats& stats = pending.stats;

        // Write suffix
        int suffixLen = static_cast<int>(term.length()) - prefixLen;
        timOut_->writeVInt(suffixLen);
        if (suffixLen > 0) {
            timOut_->writeBytes(term.data() + prefixLen, suffixLen);
        }

        // Write stats
        timOut_->writeVInt(stats.docFreq);
        timOut_->writeVLong(stats.totalTermFreq);
        timOut_->writeVLong(stats.postingsFP);
    }

    // Add block to FST
    // Use first term as prefix (simplified - real Lucene uses more sophisticated logic)
    util::BytesRef blockPrefix(prefix.data(), prefixLen);
    fstBuilder_.add(pendingTerms_[0].term, blockFP);

    // Clear pending terms
    pendingTerms_.clear();
}

void BlockTreeTermsWriter::writeFST() {
    // Finish FST construction
    auto fst = fstBuilder_.finish();

    // Write block index to .tip file
    // Format: [magic][fieldName][startFP][numTerms][numBlocks][block entries...]
    // Each block entry: [firstTerm][blockFP]

    tipOut_->writeInt(0x54495031);  // "TIP1" magic
    tipOut_->writeString(fieldInfo_.name);  // Field name
    tipOut_->writeVLong(termsStartFP_);  // Starting file pointer for this field's terms
    tipOut_->writeVLong(numTerms_);

    // Write block index (simplified replacement for FST)
    const auto& blockEntries = fstBuilder_.getEntries();
    tipOut_->writeVInt(static_cast<int>(blockEntries.size()));  // Number of blocks

    std::cerr << "[BlockTreeTermsWriter] Writing " << blockEntries.size()
              << " blocks for field '" << fieldInfo_.name << "'" << std::endl;

    for (const auto& entry : blockEntries) {
        // Write first term of block
        tipOut_->writeVInt(static_cast<int>(entry.term.length()));
        tipOut_->writeBytes(entry.term.data(), entry.term.length());

        // Write block file pointer
        tipOut_->writeVLong(entry.output);

        if (blockEntries.size() <= 10) {  // Debug for small indexes
            std::string termStr(reinterpret_cast<const char*>(entry.term.data()), entry.term.length());
            std::cerr << "  Block: firstTerm='" << termStr << "', FP=" << entry.output << std::endl;
        }
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
