// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <cstring>
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

    // CRITICAL: Seek to beginning! Multiple readers share the same IndexInput
    tipIn_->seek(0);

    bool foundField = false;
    while (tipIn_->getFilePointer() < tipIn_->length()) {
        int magic = tipIn_->readInt();

        std::string fieldName = tipIn_->readString();
        int64_t startFP = tipIn_->readVLong();
        int64_t numTerms = tipIn_->readVLong();

        if (fieldName == fieldInfo_.name) {
            // Found our field
            termsStartFP_ = startFP;
            numTerms_ = numTerms;
            foundField = true;

            if (magic == 0x54495031) {  // "TIP1" - old format with block list
                int numBlocks = tipIn_->readVInt();
                blockIndex_.reserve(numBlocks);

                for (int i = 0; i < numBlocks; i++) {
                    int termLen = tipIn_->readVInt();
                    std::vector<uint8_t> termData(termLen);
                    tipIn_->readBytes(termData.data(), termLen);
                    int64_t blockFP = tipIn_->readVLong();
                    blockIndex_.emplace_back(util::BytesRef(termData.data(), termLen), blockFP);
                }
                fst_ = std::make_unique<util::FST>();

            } else if (magic == 0x54495032) {  // "TIP2" - new format with FST
                int fstSize = tipIn_->readVInt();
                std::vector<uint8_t> fstData(fstSize);
                tipIn_->readBytes(fstData.data(), fstSize);
                fst_ = util::FST::deserialize(fstData);

                // Extract block metadata from FST for compatibility with iteration code
                // Use const reference to avoid copying 12,804 entries (~256 KB)
                const auto& fstEntries = fst_->getAllEntries();
                blockIndex_.reserve(fstEntries.size());
                for (const auto& [termBytes, blockFP] : fstEntries) {
                    blockIndex_.emplace_back(
                        util::BytesRef(termBytes.data(), termBytes.size()),
                        blockFP
                    );
                }

            } else {
                throw IOException("Invalid .tip magic: " + std::to_string(magic));
            }
            break;
        } else {
            // Skip field data
            if (magic == 0x54495031) {
                int numBlocks = tipIn_->readVInt();
                for (int i = 0; i < numBlocks; i++) {
                    int termLen = tipIn_->readVInt();
                    tipIn_->seek(tipIn_->getFilePointer() + termLen);
                    tipIn_->readVLong();
                }
            } else if (magic == 0x54495032) {
                int fstSize = tipIn_->readVInt();
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

    // Clear but reuse allocated capacity (no realloc after warmup)
    block.arena_.clear();
    block.termOffsets_.clear();
    block.termLengths_.clear();
    block.stats.clear();

    // Read all terms into flat arena - zero per-term allocations
    for (int i = 0; i < termCount; i++) {
        int suffixLen = timIn_->readVInt();
        int termLen = prefixLen + suffixLen;

        // Record offset before appending
        uint32_t offset = static_cast<uint32_t>(block.arena_.size());
        block.termOffsets_.push_back(offset);
        block.termLengths_.push_back(static_cast<uint16_t>(termLen));

        // Grow arena and write term bytes in-place
        block.arena_.resize(block.arena_.size() + termLen);
        uint8_t* dest = block.arena_.data() + offset;

        // Copy prefix
        if (prefixLen > 0) {
            std::memcpy(dest, block.prefixData.data(), prefixLen);
        }

        // Read suffix directly into arena
        if (suffixLen > 0) {
            timIn_->readBytes(dest + prefixLen, suffixLen);
        }

        // Read stats
        BlockTreeTermsWriter::TermStats stats;
        stats.docFreq = timIn_->readVInt();
        stats.totalTermFreq = timIn_->readVLong();
        stats.postingsFP = timIn_->readVLong();
        stats.skipStartFP = timIn_->readVLong();  // Block-Max WAND support
        block.stats.push_back(stats);
    }

    // Build BytesRef pointers into arena (single pass, no allocation)
    block.rebuildTermRefs();
}

std::shared_ptr<BlockTreeTermsReader::TermBlock> BlockTreeTermsReader::getCachedBlock(int blockIndex) {
    // Check cache first
    auto it = blockCache_.find(blockIndex);
    if (it != blockCache_.end()) {
        return it->second;
    }

    // Cache miss: load from disk and cache
    auto block = std::make_shared<TermBlock>();
    int64_t blockFP = blockIndex_[blockIndex].blockFP;
    loadBlock(blockFP, *block);

    blockCache_[blockIndex] = block;
    return block;
}

std::unique_ptr<index::TermsEnum> BlockTreeTermsReader::iterator() {
    return std::make_unique<SegmentTermsEnum>(this);
}

int BlockTreeTermsReader::findBlockForTerm(const util::BytesRef& term) const {
    if (blockIndex_.empty()) {
        return -1;
    }

    // Binary search to find the rightmost block where firstTerm <= term
    // This is the block that may contain the term
    auto it = std::upper_bound(
        blockIndex_.begin(), blockIndex_.end(), term,
        [](const util::BytesRef& term, const BlockMetadata& block) {
            return term < block.firstTerm;
        });

    // upper_bound returns first block where firstTerm > term
    // So we want the block before it (which has firstTerm <= term)
    if (it == blockIndex_.begin()) {
        // Term is before all blocks
        return -1;
    }

    return static_cast<int>(std::distance(blockIndex_.begin(), it) - 1);
}

// ==================== SegmentTermsEnum ====================

SegmentTermsEnum::SegmentTermsEnum(BlockTreeTermsReader* reader)
    : reader_(reader)
    , currentBlockIndex_(-1)
    , currentTermIndex_(-1)
    , positioned_(false) {}

void SegmentTermsEnum::loadBlockByIndex(int blockIndex) {
    if (blockIndex < 0 || blockIndex >= static_cast<int>(reader_->blockIndex_.size())) {
        throw std::out_of_range("Invalid block index");
    }

    currentBlockIndex_ = blockIndex;
    currentBlock_ = reader_->getCachedBlock(blockIndex);
}

bool SegmentTermsEnum::next() {
    if (!positioned_) {
        // First call - check if field is empty
        if (reader_->getNumTerms() == 0 || reader_->blockIndex_.empty()) {
            positioned_ = true;
            return false;
        }
        // Load first block
        loadBlockByIndex(0);
        currentTermIndex_ = 0;
        positioned_ = true;
        return !currentBlock_->terms.empty();
    }

    // Move to next term in current block
    currentTermIndex_++;

    if (currentTermIndex_ < static_cast<int>(currentBlock_->terms.size())) {
        return true;
    }

    // No more terms in current block - try next block
    if (currentBlockIndex_ + 1 < static_cast<int>(reader_->blockIndex_.size())) {
        loadBlockByIndex(currentBlockIndex_ + 1);
        currentTermIndex_ = 0;
        return !currentBlock_->terms.empty();
    }

    // No more blocks
    return false;
}

bool SegmentTermsEnum::seekExact(const util::BytesRef& text) {
    // Use block index to find which block may contain the term
    int blockIndex = reader_->findBlockForTerm(text);

    if (blockIndex < 0) {
        // Term is before all blocks
        positioned_ = false;
        return false;
    }

    // Load the block (uses shared cache - O(1) if already loaded)
    if (blockIndex != currentBlockIndex_) {
        loadBlockByIndex(blockIndex);
    }

    // Binary search within block
    auto it = std::lower_bound(
        currentBlock_->terms.begin(), currentBlock_->terms.end(), text,
        [](const util::BytesRef& a, const util::BytesRef& b) { return a < b; });

    if (it != currentBlock_->terms.end() && *it == text) {
        currentTermIndex_ = static_cast<int>(it - currentBlock_->terms.begin());
        positioned_ = true;
        return true;
    }

    // Not found - may need to check next block if we're at the boundary
    // But for exact match, if it's not in this block, it doesn't exist
    return false;
}

index::TermsEnum::SeekStatus SegmentTermsEnum::seekCeil(const util::BytesRef& text) {
    // Use block index to find which block may contain the term
    int blockIndex = reader_->findBlockForTerm(text);

    if (blockIndex < 0) {
        // Term is before all blocks - return first term in first block
        if (reader_->blockIndex_.empty()) {
            return SeekStatus::END;
        }
        loadBlockByIndex(0);
        currentTermIndex_ = 0;
        positioned_ = true;
        return SeekStatus::NOT_FOUND;
    }

    // Load the block (uses shared cache)
    if (blockIndex != currentBlockIndex_) {
        loadBlockByIndex(blockIndex);
    }

    // Binary search for ceiling
    auto it = std::lower_bound(
        currentBlock_->terms.begin(), currentBlock_->terms.end(), text,
        [](const util::BytesRef& a, const util::BytesRef& b) { return a < b; });

    if (it == currentBlock_->terms.end()) {
        // Term is after all terms in this block - try next block
        if (currentBlockIndex_ + 1 < static_cast<int>(reader_->blockIndex_.size())) {
            loadBlockByIndex(currentBlockIndex_ + 1);
            currentTermIndex_ = 0;
            positioned_ = true;
            return SeekStatus::NOT_FOUND;
        }
        return SeekStatus::END;
    }

    currentTermIndex_ = static_cast<int>(it - currentBlock_->terms.begin());
    positioned_ = true;

    if (*it == text) {
        return SeekStatus::FOUND;
    }

    return SeekStatus::NOT_FOUND;
}

util::BytesRef SegmentTermsEnum::term() const {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_->terms.size())) {
        return util::BytesRef();
    }
    return currentBlock_->terms[currentTermIndex_];
}

int SegmentTermsEnum::docFreq() const {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_->stats.size())) {
        return 0;
    }
    return currentBlock_->stats[currentTermIndex_].docFreq;
}

int64_t SegmentTermsEnum::totalTermFreq() const {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_->stats.size())) {
        return 0;
    }
    return currentBlock_->stats[currentTermIndex_].totalTermFreq;
}

std::unique_ptr<index::PostingsEnum> SegmentTermsEnum::postings() {
    return postings(false);
}

std::unique_ptr<index::PostingsEnum> SegmentTermsEnum::postings(bool useBatch) {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_->stats.size())) {
        throw std::runtime_error("No current term (call next() or seek first)");
    }

    if (!postingsReader_ || !fieldInfo_) {
        throw std::runtime_error("PostingsReader not set (internal error)");
    }

    // Get term state
    const auto& stats = currentBlock_->stats[currentTermIndex_];

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

std::unique_ptr<index::PostingsEnum> SegmentTermsEnum::impactsPostings() {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_->stats.size())) {
        throw std::runtime_error("No current term (call next() or seek first)");
    }

    if (!postingsReader_ || !fieldInfo_) {
        throw std::runtime_error("PostingsReader not set (internal error)");
    }

    // Get term state
    const auto& stats = currentBlock_->stats[currentTermIndex_];

    // Create TermState for postings reader
    codecs::lucene104::TermState termState;
    termState.docStartFP = stats.postingsFP;
    termState.docFreq = stats.docFreq;
    termState.totalTermFreq = stats.totalTermFreq;
    termState.skipStartFP = stats.skipStartFP;  // Block-Max WAND support

    // Cast postings reader back to correct type
    auto* reader = static_cast<codecs::lucene104::Lucene104PostingsReader*>(postingsReader_);

    // Get impacts-aware postings from reader
    auto result = reader->impactsPostings(*fieldInfo_, termState);

    return result;
}

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
