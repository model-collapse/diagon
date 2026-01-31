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
     * Term block loaded from disk.
     */
    struct TermBlock {
        /** Common prefix for all terms */
        util::BytesRef prefix;

        /** Storage for prefix bytes */
        std::vector<uint8_t> prefixData;

        /** Terms in block (full terms, not just suffixes) */
        std::vector<util::BytesRef> terms;

        /** Storage for term bytes (each term owns its bytes) */
        std::vector<std::vector<uint8_t>> termData;

        /** Statistics for each term */
        std::vector<BlockTreeTermsWriter::TermStats> stats;

        /** File pointer to this block */
        int64_t blockFP;

        TermBlock()
            : blockFP(0) {}
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

    /**
     * Load term block at given file pointer.
     *
     * @param blockFP File pointer to block
     * @param block Output: loaded block
     */
    void loadBlock(int64_t blockFP, TermBlock& block);

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
    BlockTreeTermsReader::TermBlock currentBlock_;
    int currentTermIndex_;
    bool positioned_;

    // Postings reader (type-erased to avoid circular dependency)
    void* postingsReader_{nullptr};
    const index::FieldInfo* fieldInfo_{nullptr};

    void loadBlockForTerm(const util::BytesRef& term);
};

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
