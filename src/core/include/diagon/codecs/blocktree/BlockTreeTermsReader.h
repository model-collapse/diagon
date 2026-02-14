// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/IndexInput.h"
#include "diagon/util/BytesRef.h"
#include "diagon/util/FST.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {
namespace blocktree {

/**
 * Reads terms in block-tree format.
 *
 * Based on: org.apache.lucene.codecs.blocktree.BlockTreeTermsReader
 *
 * Simplified for Phase 2 MVP - matches BlockTreeTermsWriter format.
 */
class BlockTreeTermsReader {
public:
    /**
     * Metadata for a single block.
     */
    struct BlockMetadata {
        /** First term in block */
        std::vector<uint8_t> firstTermData;
        util::BytesRef firstTerm;

        /** File pointer to block */
        int64_t blockFP;

        BlockMetadata()
            : blockFP(0) {}

        BlockMetadata(const util::BytesRef& term, int64_t fp)
            : firstTermData(term.data(), term.data() + term.length())
            , firstTerm(firstTermData.data(), firstTermData.size())
            , blockFP(fp) {}
    };

    /**
     * Term block loaded from disk.
     *
     * Uses a flat arena for all term bytes to avoid per-term heap allocations.
     * loadBlock() reuses the arena across calls (clear + append, no realloc
     * once the arena reaches steady-state size).
     */
    struct TermBlock {
        /** Common prefix for all terms */
        util::BytesRef prefix;

        /** Storage for prefix bytes */
        std::vector<uint8_t> prefixData;

        /** Terms in block (BytesRef pointing into arena_) */
        std::vector<util::BytesRef> terms;

        /** Flat arena: all term bytes packed contiguously */
        std::vector<uint8_t> arena_;

        /** Offsets into arena_ where each term starts */
        std::vector<uint32_t> termOffsets_;

        /** Lengths of each term in arena_ */
        std::vector<uint16_t> termLengths_;

        /** Statistics for each term */
        std::vector<BlockTreeTermsWriter::TermStats> stats;

        /** File pointer to this block */
        int64_t blockFP;

        TermBlock()
            : blockFP(0) {
            // Pre-allocate for typical block sizes to avoid initial allocs
            arena_.reserve(4096);
            terms.reserve(64);
            termOffsets_.reserve(64);
            termLengths_.reserve(64);
            stats.reserve(64);
        }

        /** Rebuild BytesRef pointers after arena_ is filled.
         *  Must be called after all terms are appended to arena_. */
        void rebuildTermRefs() {
            terms.resize(termOffsets_.size());
            for (size_t i = 0; i < termOffsets_.size(); i++) {
                terms[i] = util::BytesRef(arena_.data() + termOffsets_[i], termLengths_[i]);
            }
        }
    };

    /**
     * Create reader.
     *
     * @param timIn Input for term blocks (.tim file)
     * @param tipIn Input for FST index (.tip file)
     * @param fieldInfo Field information
     */
    BlockTreeTermsReader(store::IndexInput* timIn, store::IndexInput* tipIn,
                         const index::FieldInfo& fieldInfo);

    /**
     * Get terms enum for iteration.
     *
     * @return TermsEnum for this field
     */
    std::unique_ptr<index::TermsEnum> iterator();

    /**
     * Get number of terms in field.
     *
     * @return Number of terms
     */
    int64_t getNumTerms() const { return numTerms_; }

private:
    store::IndexInput* timIn_;
    store::IndexInput* tipIn_;
    const index::FieldInfo& fieldInfo_;

    std::unique_ptr<util::FST> fst_;
    int64_t numTerms_;
    int64_t termsStartFP_;  // File pointer where this field's terms start

    /** Block index: list of all blocks with their first terms */
    std::vector<BlockMetadata> blockIndex_;

    /** Shared block cache: maps block index → loaded TermBlock.
     *  All SegmentTermsEnum instances share this cache via the reader pointer.
     *  Eliminates redundant disk reads when the same blocks are accessed
     *  across multiple search() calls (e.g., repeated queries). */
    std::unordered_map<int, std::shared_ptr<TermBlock>> blockCache_;

    /**
     * Load term block at given file pointer.
     *
     * @param blockFP File pointer to block
     * @param block Output: loaded block
     */
    void loadBlock(int64_t blockFP, TermBlock& block);

    /**
     * Get a cached block by index, loading from disk if not cached.
     *
     * @param blockIndex Index into blockIndex_
     * @return Shared pointer to cached TermBlock
     */
    std::shared_ptr<TermBlock> getCachedBlock(int blockIndex);

    /**
     * Find block that may contain the given term.
     * Returns block index, or -1 if term is before all blocks.
     *
     * @param term Term to search for
     * @return Block index in blockIndex_
     */
    int findBlockForTerm(const util::BytesRef& term) const;

    friend class SegmentTermsEnum;
};

/**
 * TermsEnum implementation for block tree format.
 */
class SegmentTermsEnum : public index::TermsEnum {
public:
    /**
     * Create enum.
     *
     * @param reader Parent reader
     */
    explicit SegmentTermsEnum(BlockTreeTermsReader* reader);

    // TermsEnum interface
    bool next() override;
    bool seekExact(const util::BytesRef& text) override;
    SeekStatus seekCeil(const util::BytesRef& text) override;
    util::BytesRef term() const override;
    int docFreq() const override;
    int64_t totalTermFreq() const override;
    std::unique_ptr<index::PostingsEnum> postings() override;
    std::unique_ptr<index::PostingsEnum> postings(bool useBatch) override;

    /**
     * Get impacts-aware postings for WAND optimization.
     * Returns PostingsEnum with skip entry support for accurate max score computation.
     *
     * @return Impacts-aware PostingsEnum
     */
    std::unique_ptr<index::PostingsEnum> impactsPostings();

    /**
     * Set postings reader for retrieving postings.
     * Must be called before calling postings().
     *
     * @param postingsReader Postings reader instance
     * @param fieldInfo Field metadata
     */
    void setPostingsReader(void* postingsReader, const index::FieldInfo* fieldInfo) {
        postingsReader_ = postingsReader;
        fieldInfo_ = fieldInfo;
    }

private:
    BlockTreeTermsReader* reader_;
    std::shared_ptr<BlockTreeTermsReader::TermBlock> currentBlock_;  // Shared from reader cache
    int currentBlockIndex_;  // Which block we're in (index into blockIndex_)
    int currentTermIndex_;   // Which term within the block
    bool positioned_;

    // Postings reader (type-erased to avoid circular dependency)
    void* postingsReader_{nullptr};
    const index::FieldInfo* fieldInfo_{nullptr};

    /**
     * Load a specific block by its index in blockIndex_.
     *
     * @param blockIndex Index into reader_->blockIndex_
     */
    void loadBlockByIndex(int blockIndex);

    /**
     * Load the block that may contain the given term.
     *
     * @param term Term to search for
     */
    void loadBlockForTerm(const util::BytesRef& term);
};

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
