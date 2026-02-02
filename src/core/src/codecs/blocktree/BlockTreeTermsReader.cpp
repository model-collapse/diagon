// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace blocktree {

// ==================== BlockTreeTermsReader ====================

BlockTreeTermsReader::BlockTreeTermsReader(store::IndexInput* timIn, store::IndexInput* tipIn,
                                           const index::FieldInfo& fieldInfo)
    : timIn_(timIn)
    , tipIn_(tipIn)
    , fieldInfo_(fieldInfo)
    , numTerms_(0) {
    if (!timIn_ || !tipIn_) {
        throw std::invalid_argument("Input streams cannot be null");
    }

    // Read FST from .tip file
    // Format: [magic][fieldName][startFP][numTerms][FST data] (per field)
    // Need to find the section for our field
    bool foundField = false;
    while (tipIn_->getFilePointer() < tipIn_->length()) {
        int magic = tipIn_->readInt();
        if (magic != 0x54495031) {  // "TIP1"
            throw IOException("Invalid .tip file magic");
        }

        std::string fieldName = tipIn_->readString();
        int64_t startFP = tipIn_->readVLong();  // Read starting file pointer
        int64_t numTerms = tipIn_->readVLong();
        int fstSize = tipIn_->readVInt();

        if (fieldName == fieldInfo_.name) {
            // Found our field
            termsStartFP_ = startFP;
            numTerms_ = numTerms;
            foundField = true;

            // Read FST (placeholder for Phase 2 MVP)
            if (fstSize > 0) {
                // TODO: Deserialize FST
                throw std::runtime_error("FST deserialization not yet implemented");
            }

            // For MVP, create empty FST
            fst_ = std::make_unique<util::FST>();
            break;
        } else {
            // Skip this field's FST data
            if (fstSize > 0) {
                tipIn_->seek(tipIn_->getFilePointer() + fstSize);
            }
        }
    }

    if (!foundField) {
        throw IOException("Field not found in .tip file: " + fieldInfo_.name);
    }
}

void BlockTreeTermsReader::loadBlock(int64_t blockFP, TermBlock& block) {
    block.blockFP = blockFP;

    // Seek to block
    timIn_->seek(blockFP);

    // Read block header
    int prefixLen = timIn_->readVInt();

    if (prefixLen > 0) {
        block.prefixData.resize(prefixLen);
        timIn_->readBytes(block.prefixData.data(), prefixLen);
        block.prefix = util::BytesRef(block.prefixData.data(), prefixLen);
    } else {
        block.prefix = util::BytesRef();
        block.prefixData.clear();
    }

    int termCount = timIn_->readVInt();
    block.terms.clear();
    block.termData.clear();
    block.stats.clear();
    block.terms.reserve(termCount);
    block.termData.reserve(termCount);
    block.stats.reserve(termCount);

    // Read each term
    for (int i = 0; i < termCount; i++) {
        // Read suffix
        int suffixLen = timIn_->readVInt();

        // Create term bytes storage
        std::vector<uint8_t> termBytes(prefixLen + suffixLen);

        // Copy prefix
        if (prefixLen > 0) {
            std::copy(block.prefixData.data(), block.prefixData.data() + prefixLen,
                      termBytes.begin());
        }

        // Read suffix
        if (suffixLen > 0) {
            timIn_->readBytes(termBytes.data() + prefixLen, suffixLen);
        }

        // Store term bytes
        block.termData.push_back(std::move(termBytes));

        // Create BytesRef pointing to stored data
        const auto& storedBytes = block.termData.back();
        block.terms.emplace_back(storedBytes.data(), storedBytes.size());

        // Read stats
        BlockTreeTermsWriter::TermStats stats;
        stats.docFreq = timIn_->readVInt();
        stats.totalTermFreq = timIn_->readVLong();
        stats.postingsFP = timIn_->readVLong();
        block.stats.push_back(stats);
    }
}

std::unique_ptr<index::TermsEnum> BlockTreeTermsReader::iterator() {
    return std::make_unique<SegmentTermsEnum>(this);
}

// ==================== SegmentTermsEnum ====================

SegmentTermsEnum::SegmentTermsEnum(BlockTreeTermsReader* reader)
    : reader_(reader)
    , currentTermIndex_(-1)
    , positioned_(false) {}

bool SegmentTermsEnum::next() {
    if (!positioned_) {
        // First call - check if field is empty
        if (reader_->getNumTerms() == 0) {
            positioned_ = true;
            return false;
        }
        // Load first block at this field's starting file pointer
        reader_->loadBlock(reader_->termsStartFP_, currentBlock_);
        currentTermIndex_ = 0;
        positioned_ = true;
        return !currentBlock_.terms.empty();
    }

    // Move to next term in current block
    currentTermIndex_++;

    if (currentTermIndex_ < static_cast<int>(currentBlock_.terms.size())) {
        return true;
    }

    // No more terms in current block
    // For Phase 2 MVP, we don't support multi-block iteration yet
    // (would need to track next block FP)
    return false;
}

bool SegmentTermsEnum::seekExact(const util::BytesRef& text) {
    // Use FST to find block
    // For Phase 2 MVP without working FST, do linear scan
    // TODO: Implement FST-based seek

    // Load first block (simplified for MVP)
    reader_->loadBlock(reader_->termsStartFP_, currentBlock_);

    // Binary search within block
    auto it = std::lower_bound(
        currentBlock_.terms.begin(), currentBlock_.terms.end(), text,
        [](const util::BytesRef& a, const util::BytesRef& b) { return a < b; });

    if (it != currentBlock_.terms.end() && *it == text) {
        currentTermIndex_ = static_cast<int>(it - currentBlock_.terms.begin());
        positioned_ = true;
        return true;
    }

    return false;
}

index::TermsEnum::SeekStatus SegmentTermsEnum::seekCeil(const util::BytesRef& text) {
    // Load first block (simplified for MVP)
    reader_->loadBlock(reader_->termsStartFP_, currentBlock_);

    // Binary search for ceiling
    auto it = std::lower_bound(
        currentBlock_.terms.begin(), currentBlock_.terms.end(), text,
        [](const util::BytesRef& a, const util::BytesRef& b) { return a < b; });

    if (it == currentBlock_.terms.end()) {
        return SeekStatus::END;
    }

    currentTermIndex_ = static_cast<int>(it - currentBlock_.terms.begin());
    positioned_ = true;

    if (*it == text) {
        return SeekStatus::FOUND;
    }

    return SeekStatus::NOT_FOUND;
}

util::BytesRef SegmentTermsEnum::term() const {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_.terms.size())) {
        return util::BytesRef();
    }
    return currentBlock_.terms[currentTermIndex_];
}

int SegmentTermsEnum::docFreq() const {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_.stats.size())) {
        return 0;
    }
    return currentBlock_.stats[currentTermIndex_].docFreq;
}

int64_t SegmentTermsEnum::totalTermFreq() const {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_.stats.size())) {
        return 0;
    }
    return currentBlock_.stats[currentTermIndex_].totalTermFreq;
}

std::unique_ptr<index::PostingsEnum> SegmentTermsEnum::postings() {
    return postings(false);
}

std::unique_ptr<index::PostingsEnum> SegmentTermsEnum::postings(bool useBatch) {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_.stats.size())) {
        throw std::runtime_error("No current term (call next() or seek first)");
    }

    if (!postingsReader_ || !fieldInfo_) {
        throw std::runtime_error("PostingsReader not set (internal error)");
    }

    // Get term state
    const auto& stats = currentBlock_.stats[currentTermIndex_];

    // Create TermState for postings reader
    codecs::lucene104::TermState termState;
    termState.docStartFP = stats.postingsFP;
    termState.docFreq = stats.docFreq;
    termState.totalTermFreq = stats.totalTermFreq;

    // Cast postings reader back to correct type
    auto* reader = static_cast<codecs::lucene104::Lucene104PostingsReader*>(postingsReader_);

    // Get postings from reader
    return reader->postings(*fieldInfo_, termState, useBatch);
}

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
