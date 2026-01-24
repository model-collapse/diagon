// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/util/ByteBlockPool.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace index {

// FieldInfosBuilder defined in FieldInfo.h
class FieldInfosBuilder;

/**
 * FreqProxTermsWriter - In-memory posting list builder
 *
 * Based on: org.apache.lucene.index.FreqProxTermsWriter
 *
 * Builds in-memory posting lists during document indexing:
 * - Tokenizes fields
 * - Stores term bytes in ByteBlockPool
 * - Stores [docID, freq] pairs in IntBlockPool
 * - Maintains term → posting list offset mapping
 *
 * Memory Layout:
 *   ByteBlockPool: "term1\0term2\0term3\0..."
 *   IntBlockPool:  [docID, freq, docID, freq, ...]
 *
 * Posting List Format (Phase 2 - Simplified):
 *   - Absolute docIDs (not deltas)
 *   - Format: [docID_abs, freq, docID_abs, freq, ...]
 *   - Delta encoding happens during flush to codec
 *
 * Thread Safety: NOT thread-safe (per-thread instance in DWPT)
 */
class FreqProxTermsWriter {
public:
    /**
     * Constructor
     * @param fieldInfosBuilder Field metadata tracker (reference)
     */
    explicit FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder);

    /**
     * Destructor
     */
    ~FreqProxTermsWriter() = default;

    // Disable copy/move
    FreqProxTermsWriter(const FreqProxTermsWriter&) = delete;
    FreqProxTermsWriter& operator=(const FreqProxTermsWriter&) = delete;
    FreqProxTermsWriter(FreqProxTermsWriter&&) = delete;
    FreqProxTermsWriter& operator=(FreqProxTermsWriter&&) = delete;

    /**
     * Add document to in-memory posting lists
     *
     * @param doc Document to add
     * @param docID Absolute document ID in segment
     */
    void addDocument(const document::Document& doc, int docID);

    /**
     * Get bytes used (approximate)
     */
    int64_t bytesUsed() const;

    /**
     * Get posting list for term (for testing/debugging)
     * Returns vector of [docID, freq] pairs
     *
     * @param term Term to lookup
     * @return Vector of [docID, freq] pairs, empty if term not found
     */
    std::vector<int> getPostingList(const std::string& term) const;

    /**
     * Get all terms (for testing/debugging)
     * Returns sorted list of unique terms
     */
    std::vector<std::string> getTerms() const;

    /**
     * Reset for reuse across segments
     * Clears all data but keeps allocated memory
     */
    void reset();

    /**
     * Clear all memory
     * Releases all allocated blocks
     */
    void clear();

private:
    /**
     * Posting list data stored in termToPosting_ map
     *
     * Note: For Phase 2 simplicity, posting lists are stored as vectors.
     * Phase 3 will use ByteBlockPool/IntBlockPool with linked continuation blocks.
     */
    struct PostingData {
        int lastDocID;              // Last docID added (for duplicate detection)
        std::vector<int> postings;  // [docID, freq, docID, freq, ...]
    };

    // Term byte storage
    util::ByteBlockPool termBytePool_;

    // Term → posting list mapping
    // Key: term string, Value: posting data
    std::unordered_map<std::string, PostingData> termToPosting_;

    // Field metadata tracker (reference)
    FieldInfosBuilder& fieldInfosBuilder_;

    /**
     * Add term occurrence to posting list
     *
     * @param fieldName Field name
     * @param term Term text
     * @param docID Document ID
     * @param indexOptions Index options for field
     */
    void addTermOccurrence(
        const std::string& fieldName,
        const std::string& term,
        int docID,
        IndexOptions indexOptions);

    /**
     * Create new posting list for term
     *
     * @param term Term text
     * @param docID Document ID
     * @return Posting data
     */
    PostingData createPostingList(const std::string& term, int docID);

    /**
     * Append docID/freq to existing posting list
     *
     * @param data Posting data to update
     * @param docID Document ID
     */
    void appendToPostingList(PostingData& data, int docID);
};

}  // namespace index
}  // namespace diagon
