// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef HAVE_LZ4
#include <lz4.h>
#endif

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

            } else if (magic == 0x54495032) {  // "TIP2" - old format with FST
                int fstSize = tipIn_->readVInt();
                std::vector<uint8_t> fstData(fstSize);
                tipIn_->readBytes(fstData.data(), fstSize);
                fst_ = util::FST::deserialize(fstData);

                // Extract block metadata from FST for compatibility with iteration code
                const auto& fstEntries = fst_->getAllEntries();
                blockIndex_.reserve(fstEntries.size());
                for (const auto& [termBytes, blockFP] : fstEntries) {
                    blockIndex_.emplace_back(util::BytesRef(termBytes.data(), termBytes.size()),
                                             blockFP);
                }

            } else if (magic == 0x54495033) {  // "TIP3" - compact block list (no FST trie)
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

            } else {
                throw IOException("Invalid .tip magic: " + std::to_string(magic));
            }
            break;
        } else {
            // Skip field data
            if (magic == 0x54495031 || magic == 0x54495033) {
                // TIP1 and TIP3: flat block list
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

void BlockTreeTermsReader::loadBlock(int64_t blockFP, const util::BytesRef& blockFirstTerm,
                                     TermBlock& block) {
    block.blockFP = blockFP;
    timIn_->seek(blockFP);

    // Read block header: VInt(code) where code = (termCount << 1) | isLastInFloor
    int code = timIn_->readVInt();
    int termCount = code >> 1;
    // bool isLastInFloor = (code & 1) != 0;  // unused (no floor blocks yet)

    // Clear but reuse allocated capacity (no realloc after warmup)
    block.arena_.clear();
    block.termOffsets_.clear();
    block.termLengths_.clear();
    block.stats.clear();
    block.stats.resize(termCount);

    // === Section 1: Suffix data (with optional LZ4 decompression) ===
    int64_t suffixHeader = timIn_->readVLong();
    int suffixRawSize = static_cast<int>(static_cast<uint64_t>(suffixHeader) >> 3);
    bool lz4Compressed = (suffixHeader & 0x01) != 0;

    std::vector<uint8_t> suffixData(suffixRawSize);
    if (lz4Compressed) {
        int compressedSize = timIn_->readVInt();
        std::vector<uint8_t> compBuf(compressedSize);
        timIn_->readBytes(compBuf.data(), compressedSize);
#ifdef HAVE_LZ4
        int result = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compBuf.data()),
            reinterpret_cast<char*>(suffixData.data()),
            compressedSize, suffixRawSize);
        if (result != suffixRawSize) {
            throw IOException("LZ4 decompression failed in .tim block");
        }
#else
        throw IOException("LZ4 compressed .tim block but LZ4 not available");
#endif
    } else {
        timIn_->readBytes(suffixData.data(), suffixRawSize);
    }

    // Parse new suffix format: [lengths section] + [concatenated suffix bytes]
    {
        int cursor = 0;

        // Decode suffix lengths section header (VInt from buffer)
        int lengthCode = 0;
        {
            uint8_t b = suffixData[cursor++];
            lengthCode = b & 0x7F;
            for (int shift = 7; (b & 0x80) != 0; shift += 7) {
                b = suffixData[cursor++];
                lengthCode |= (static_cast<int>(b & 0x7F)) << shift;
            }
        }

        bool allEqual = (lengthCode & 1) != 0;
        int numBytes = lengthCode >> 1;

        // Read suffix lengths
        std::vector<uint8_t> suffixLens(termCount);
        if (allEqual) {
            uint8_t commonLen = suffixData[cursor++];
            std::fill(suffixLens.begin(), suffixLens.end(), commonLen);
        } else {
            for (int i = 0; i < numBytes && i < termCount; i++) {
                suffixLens[i] = suffixData[cursor++];
            }
        }

        // Derive prefix from block's first term and first suffix length
        int prefixLen = 0;
        if (termCount > 0) {
            prefixLen = static_cast<int>(blockFirstTerm.length()) - suffixLens[0];
            if (prefixLen < 0) prefixLen = 0;
        }

        if (prefixLen > 0) {
            block.prefixData.resize(prefixLen);
            std::memcpy(block.prefixData.data(), blockFirstTerm.data(), prefixLen);
            block.prefix = util::BytesRef(block.prefixData.data(), prefixLen);
        } else {
            block.prefixData.clear();
            block.prefix = util::BytesRef();
        }

        // Read suffix bytes and reconstruct full terms
        for (int i = 0; i < termCount; i++) {
            int suffixLen = suffixLens[i];
            int termLen = prefixLen + suffixLen;

            uint32_t offset = static_cast<uint32_t>(block.arena_.size());
            block.termOffsets_.push_back(offset);
            block.termLengths_.push_back(static_cast<uint16_t>(termLen));

            block.arena_.resize(block.arena_.size() + termLen);
            uint8_t* dest = block.arena_.data() + offset;

            if (prefixLen > 0) {
                std::memcpy(dest, block.prefixData.data(), prefixLen);
            }
            if (suffixLen > 0) {
                std::memcpy(dest + prefixLen, &suffixData[cursor], suffixLen);
                cursor += suffixLen;
            }
        }
    }

    // === Section 2: Stats (column-stride with singleton RLE) ===
    {
        int statsSize = timIn_->readVInt();
        int64_t statsEnd = timIn_->getFilePointer() + statsSize;

        int termIdx = 0;
        while (termIdx < termCount) {
            int32_t val = timIn_->readVInt();
            if (val & 1) {
                // Singleton run: docFreq=1, totalTermFreq=1
                int runCount = (val >> 1) + 1;
                for (int j = 0; j < runCount && termIdx < termCount; j++) {
                    block.stats[termIdx].docFreq = 1;
                    block.stats[termIdx].totalTermFreq = 1;
                    termIdx++;
                }
            } else {
                block.stats[termIdx].docFreq = val >> 1;
                int64_t delta = timIn_->readVLong();
                block.stats[termIdx].totalTermFreq = block.stats[termIdx].docFreq + delta;
                termIdx++;
            }
        }
        // Seek to end of stats section for forward compatibility
        timIn_->seek(statsEnd);
    }

    // === Section 3: Metadata (conditional column-stride file pointer deltas) ===
    // Format: flags byte + [postingsFP column] + [posStartFP column?] + [skipStartFP section?]
    {
        int metaSize = timIn_->readVInt();
        int64_t metaEnd = timIn_->getFilePointer() + metaSize;

        // Read flags byte
        uint8_t flags = timIn_->readByte();
        bool hasPosFP  = (flags & 0x02) != 0;
        bool hasSkipFP = (flags & 0x04) != 0;

        // Column 1: postingsFP (always present)
        int64_t lastFP = 0;
        for (int i = 0; i < termCount; i++) {
            block.stats[i].postingsFP = lastFP + timIn_->readVLong();
            lastFP = block.stats[i].postingsFP;
        }

        // Column 2: posStartFP (conditional)
        if (hasPosFP) {
            lastFP = 0;
            for (int i = 0; i < termCount; i++) {
                block.stats[i].posStartFP = lastFP + timIn_->readVLong();
                lastFP = block.stats[i].posStartFP;
            }
        } else {
            for (int i = 0; i < termCount; i++) {
                block.stats[i].posStartFP = -1;
            }
        }

        // Column 3: skipStartFP (sparse bitmap + values)
        if (hasSkipFP) {
            // Read bitmap
            int bitmapBytes = (termCount + 7) / 8;
            std::vector<uint8_t> bitmap(bitmapBytes);
            timIn_->readBytes(bitmap.data(), bitmapBytes);

            // Read delta-encoded FPs for set bits only
            lastFP = 0;
            for (int i = 0; i < termCount; i++) {
                if (bitmap[i / 8] & (1 << (i % 8))) {
                    block.stats[i].skipStartFP = lastFP + timIn_->readVLong();
                    lastFP = block.stats[i].skipStartFP;
                } else {
                    block.stats[i].skipStartFP = -1;
                }
            }
        } else {
            for (int i = 0; i < termCount; i++) {
                block.stats[i].skipStartFP = -1;
            }
        }

        // Seek to end of metadata section for forward compatibility
        timIn_->seek(metaEnd);
    }

    // Build BytesRef pointers into arena (single pass, no allocation)
    block.rebuildTermRefs();
}

std::shared_ptr<BlockTreeTermsReader::TermBlock>
BlockTreeTermsReader::getCachedBlock(int blockIndex) {
    // Check cache first
    auto it = blockCache_.find(blockIndex);
    if (it != blockCache_.end()) {
        return it->second;
    }

    // Cache miss: load from disk and cache
    auto block = std::make_shared<TermBlock>();
    loadBlock(blockIndex_[blockIndex].blockFP, blockIndex_[blockIndex].firstTerm, *block);

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
    auto it = std::upper_bound(blockIndex_.begin(), blockIndex_.end(), term,
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
    termState.posStartFP = stats.posStartFP;
    termState.docFreq = stats.docFreq;
    termState.totalTermFreq = stats.totalTermFreq;

    // Cast postings reader back to correct type
    auto* reader = static_cast<codecs::lucene104::Lucene104PostingsReader*>(postingsReader_);

    // Get postings from reader
    return reader->postings(*fieldInfo_, termState, useBatch);
}

std::unique_ptr<index::PostingsEnum> SegmentTermsEnum::postings(int features) {
    if (!positioned_ || currentTermIndex_ < 0 ||
        currentTermIndex_ >= static_cast<int>(currentBlock_->stats.size())) {
        throw std::runtime_error("No current term (call next() or seek first)");
    }

    if (!postingsReader_ || !fieldInfo_) {
        throw std::runtime_error("PostingsReader not set (internal error)");
    }

    // If positions requested, use the position-aware PostingsEnum
    if (features & index::FEATURE_POSITIONS) {
        const auto& stats = currentBlock_->stats[currentTermIndex_];

        codecs::lucene104::TermState termState;
        termState.docStartFP = stats.postingsFP;
        termState.posStartFP = stats.posStartFP;
        termState.docFreq = stats.docFreq;
        termState.totalTermFreq = stats.totalTermFreq;

        auto* reader = static_cast<codecs::lucene104::Lucene104PostingsReader*>(postingsReader_);
        return reader->postingsWithPositions(*fieldInfo_, termState);
    }

    // Otherwise, fall back to regular postings
    return postings(false);
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
    termState.posStartFP = stats.posStartFP;
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
