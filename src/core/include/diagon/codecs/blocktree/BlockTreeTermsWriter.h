// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/IndexOutput.h"
#include "diagon/util/BytesRef.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace codecs {
namespace blocktree {

/**
 * Writes terms in block-tree format with FST prefix index.
 *
 * Based on: org.apache.lucene.codecs.blocktree.BlockTreeTermsWriter
 *
 * Simplified for Phase 2 MVP:
 * - Fixed block size (25-48 terms per block)
 * - No compression initially
 * - Simple FST for term → block mapping
 * - No floor blocks (will add later)
 *
 * File format (.tim):
 *   Per block: VInt((termCount << 1) | isLastInFloor)
 *   Section 1 - Suffixes: VLong((rawSize << 3) | flags) [+ VInt(compSize) if LZ4]
 *     Suffix lengths: VInt((numBytes << 1) | allEqual)
 *       allEqual=1: 1 byte commonLen; allEqual=0: numBytes individual uint8 lengths
 *     Followed by concatenated suffix bytes; optionally LZ4 compressed
 *   Section 2 - Stats:   VInt(statsSize) + singleton RLE encoded docFreq/totalTermFreq
 *     Singleton run: VInt(((count-1) << 1) | 1)
 *     Non-singleton: VInt((docFreq << 1) | 0) + VLong(totalTermFreq - docFreq)
 *   Section 3 - Metadata: VInt(metaSize) + conditional column-stride delta-encoded FPs
 *     byte(flags): bit0=postingsFP(always), bit1=posStartFP, bit2=skipStartFP
 *     [all postingsFP deltas]
 *     [all posStartFP deltas]  (if bit1: field has positions)
 *     [skipStartFP bitmap + sparse deltas]  (if bit2: block has skip data)
 *       bitmap: ceil(termCount/8) bytes, 1 bit per term
 *       deltas: VLong delta-encoded FPs for set bits only
 * - .tip: Compact block list mapping first terms to block file pointers
 */
class BlockTreeTermsWriter {
public:
    /**
     * Configuration for block tree.
     */
    struct Config {
        /** Minimum items in a block before splitting */
        int minItemsInBlock;

        /** Maximum items in a block */
        int maxItemsInBlock;

        /** Constructor */
        Config()
            : minItemsInBlock(25)
            , maxItemsInBlock(48) {}  // Proper block size with multi-block support
    };

    /**
     * Term block statistics.
     */
    struct TermStats {
        /** Document frequency */
        int docFreq = 0;

        /** Total term frequency */
        int64_t totalTermFreq = 0;

        /** Postings file pointer */
        int64_t postingsFP = 0;

        /** Skip file pointer (Block-Max WAND support) */
        int64_t skipStartFP = -1;

        /** Position data file pointer (-1 = no positions) */
        int64_t posStartFP = -1;

        /** Constructor */
        TermStats() = default;
        TermStats(int df, int64_t ttf, int64_t fp, int64_t skip = -1, int64_t pos = -1)
            : docFreq(df)
            , totalTermFreq(ttf)
            , postingsFP(fp)
            , skipStartFP(skip)
            , posStartFP(pos) {}
    };

    /**
     * Create writer for a single field.
     *
     * @param timOut Output for term blocks (.tim file)
     * @param tipOut Output for FST index (.tip file)
     * @param fieldInfo Field information
     * @param config Block configuration
     */
    BlockTreeTermsWriter(store::IndexOutput* timOut, store::IndexOutput* tipOut,
                         const index::FieldInfo& fieldInfo, const Config& config = {});

    /**
     * Add a term with its statistics.
     * Terms must be added in sorted (UTF-8 byte) order.
     *
     * @param term Term to add
     * @param stats Term statistics
     */
    void addTerm(const util::BytesRef& term, const TermStats& stats);

    /**
     * Finish writing all terms and write FST index.
     */
    void finish();

    /**
     * Get field-level statistics (valid after finish())
     */
    int64_t getNumTerms() const { return numTerms_; }
    int64_t getSumTotalTermFreq() const { return sumTotalTermFreq_; }
    int64_t getSumDocFreq() const { return sumDocFreq_; }
    int getDocCount() const { return docCount_; }

    /**
     * Set document count (must be called before finish())
     */
    void setDocCount(int docCount) { docCount_ = docCount; }

private:
    /**
     * Pending term in the current block.
     */
    struct PendingTerm {
        std::vector<uint8_t> termData;  // Owns the term bytes
        util::BytesRef term;
        TermStats stats;

        PendingTerm(const util::BytesRef& t, const TermStats& s)
            : termData(t.data(), t.data() + t.length())
            , term(termData.data(), termData.size())
            , stats(s) {}
    };

    /**
     * Pending block reference (for FST).
     */
    struct PendingBlock {
        std::vector<uint8_t> prefixData;  // Owns the prefix bytes
        util::BytesRef prefix;
        int64_t blockFP;

        PendingBlock(const util::BytesRef& p, int64_t fp)
            : prefixData(p.data(), p.data() + p.length())
            , prefix(prefixData.data(), prefixData.size())
            , blockFP(fp) {}
    };

    store::IndexOutput* timOut_;
    store::IndexOutput* tipOut_;
    const index::FieldInfo& fieldInfo_;
    Config config_;

    std::vector<PendingTerm> pendingTerms_;
    util::BytesRef lastTerm_;
    std::vector<uint8_t> lastTermData_;  // Storage for lastTerm_
    int64_t numTerms_;
    int64_t termsStartFP_;  // File pointer where this field's terms start
    bool finished_;

    // Field-level statistics
    int64_t sumTotalTermFreq_;  // Sum of all term frequencies in field
    int64_t sumDocFreq_;        // Sum of all document frequencies in field
    int docCount_;              // Number of documents with this field

    /** Compact block index: (firstTerm, blockFP) per block.
     *  Replaces FST::Builder — we write a flat block list to .tip
     *  instead of the full packed FST trie (~200 KB savings for Reuters). */
    struct BlockEntry {
        std::vector<uint8_t> termData;
        int64_t blockFP;

        BlockEntry(const uint8_t* data, size_t len, int64_t fp)
            : termData(data, data + len)
            , blockFP(fp) {}
    };
    std::vector<BlockEntry> blockEntries_;

    void writeBlock();
    void writeBlockIndex();
    int sharedPrefixLength(const util::BytesRef& a, const util::BytesRef& b) const;
};

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
