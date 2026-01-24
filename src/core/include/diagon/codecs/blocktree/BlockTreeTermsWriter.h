// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/IndexOutput.h"
#include "diagon/util/BytesRef.h"
#include "diagon/util/FST.h"
#include "diagon/codecs/PostingsFormat.h"

#include <memory>
#include <vector>
#include <string>

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
 * File format:
 * - .tim: Term blocks with shared prefix compression
 * - .tip: FST index mapping prefixes to block file pointers
 * - .tmd: Field metadata (optional for MVP)
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
        Config() : minItemsInBlock(25), maxItemsInBlock(48) {}
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

        /** Constructor */
        TermStats() = default;
        TermStats(int df, int64_t ttf, int64_t fp)
            : docFreq(df), totalTermFreq(ttf), postingsFP(fp) {}
    };

    /**
     * Create writer for a single field.
     *
     * @param timOut Output for term blocks (.tim file)
     * @param tipOut Output for FST index (.tip file)
     * @param fieldInfo Field information
     * @param config Block configuration
     */
    BlockTreeTermsWriter(
        store::IndexOutput* timOut,
        store::IndexOutput* tipOut,
        const index::FieldInfo& fieldInfo,
        const Config& config = {});

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
    bool finished_;

    util::FST::Builder fstBuilder_;

    void writeBlock();
    void writeFST();
    int sharedPrefixLength(const util::BytesRef& a, const util::BytesRef& b) const;
};

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
