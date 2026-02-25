// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/util/ByteBlockPool.h"
#include "diagon/util/IntBlockPool.h"

#include <memory>
#include <set>
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
 * Posting List Format:
 *   - Absolute docIDs (not deltas)
 *   - Without positions: [docID, freq, docID, freq, ...]
 *   - With positions: [docID, freq, pos0, ..., posN, docID, freq, pos0, ...]
 *   - Delta encoding happens during flush to codec
 *
 * Thread Safety: NOT thread-safe (per-thread instance in DWPT)
 */
class FreqProxTermsWriter {
public:
    /**
     * Constructor
     * @param fieldInfosBuilder Field metadata tracker (reference)
     * @param expectedTerms Expected number of unique terms (for pre-sizing hash table)
     */
    explicit FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder,
                                 size_t expectedTerms = 10000);

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
     * Get posting list for field-specific term
     *
     * @param field Field name
     * @param term Term text
     * @return Vector of [docID, freq] pairs, empty if term not found
     */
    std::vector<int> getPostingList(const std::string& field, const std::string& term) const;

    /**
     * Get all terms for a specific field
     *
     * @param field Field name
     * @return Sorted list of unique terms in field
     */
    std::vector<std::string> getTermsForField(const std::string& field) const;

    /**
     * Field statistics for Terms implementation
     * Computed incrementally during indexing to avoid scanning posting lists during flush
     */
    struct FieldStats {
        int64_t sumTotalTermFreq = 0;  // Sum of all term frequencies in field
        int64_t sumDocFreq = 0;        // Sum of document frequencies (docs per term)
        int docCount = 0;              // Number of unique documents with this field
    };

    /**
     * Get field statistics (pre-computed during indexing)
     *
     * @param fieldName Field name
     * @return Field statistics, or default-initialized if field not found
     */
    FieldStats getFieldStats(const std::string& fieldName) const;

    /**
     * Get field lengths for norm computation
     *
     * @return Map of (fieldName, docID) -> fieldLength
     */
    const std::unordered_map<std::string, std::unordered_map<int, int>>& getFieldLengths() const {
        return fieldLengths_;
    }

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
     * Uses aggressive pre-allocation to minimize vector reallocation overhead.
     * Typical posting list has ~10-50 postings, so we pre-allocate 100 ints (50 postings).
     */
    struct PostingData {
        int lastDocID;              // Last docID added (for duplicate detection)
        std::vector<int> postings;  // [docID, freq, docID, freq, ...]
    };

    // Custom hash function for pair<int, string>
    // Uses field ID (integer) instead of field name string for faster hashing
    struct FieldTermHash {
        size_t operator()(const std::pair<int, std::string>& p) const {
            // Hash field ID (integer) + term (string)
            // Integer hashing is ~10x faster than string hashing
            size_t h1 = std::hash<int>{}(p.first);
            size_t h2 = std::hash<std::string>{}(p.second);
            // Better hash combining than XOR (from Boost)
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    // Field name to field ID mapping (assigned incrementally)
    // Reduces hash computation overhead: hash(int, string) vs hash(string, string)
    std::unordered_map<std::string, int> fieldNameToId_;
    int nextFieldId_ = 0;

    // Term byte storage
    util::ByteBlockPool termBytePool_;

    // Term → posting list mapping
    // Key: (fieldID, term) pair - uses integer field ID instead of string for faster hashing
    // Integer hashing is ~10x faster than string hashing
    std::unordered_map<std::pair<int, std::string>, PostingData, FieldTermHash> termToPosting_;

    // Field metadata tracker (reference)
    FieldInfosBuilder& fieldInfosBuilder_;

    // Reusable term positions map (avoids allocating one per document)
    // Cleared and reused for each document to reduce malloc overhead
    // Maps term -> list of positions within the document
    std::unordered_map<std::string, std::vector<int>> termPositionsCache_;

    // Field lengths for norm computation: fieldName -> (docID -> fieldLength)
    std::unordered_map<std::string, std::unordered_map<int, int>> fieldLengths_;

    // Incremental memory usage tracking (like Lucene's approach)
    // Track memory as we add terms, not by scanning all posting lists
    int64_t bytesUsed_ = 0;

    // Incremental field statistics (computed during indexing, not during flush)
    // Eliminates need to scan all posting lists during FreqProxTerms construction
    std::unordered_map<std::string, FieldStats> fieldStats_;

    // Pre-sorted term index per field (maintained incrementally during indexing)
    // Eliminates O(n log n) sorting during flush - getTermsForField() is now O(k)
    // Trade-off: O(log n) insert per unique term vs O(n log n) sort on every flush
    std::unordered_map<std::string, std::set<std::string>> fieldToSortedTerms_;

    /**
     * Add term occurrence to posting list
     *
     * @param fieldName Field name
     * @param term Term text
     * @param docID Document ID
     * @param freq Term frequency in document
     * @param positions Term positions in document (empty if positions not indexed)
     * @param indexOptions Index options for field
     */
    void addTermOccurrence(const std::string& fieldName, const std::string& term, int docID,
                           int freq, const std::vector<int>& positions, IndexOptions indexOptions);

    /**
     * Create new posting list for term
     *
     * @param term Term text
     * @param docID Document ID
     * @param freq Term frequency in document
     * @param positions Term positions (appended after freq if non-empty)
     * @return Posting data
     */
    PostingData createPostingList(const std::string& term, int docID, int freq,
                                  const std::vector<int>& positions);

    /**
     * Append docID/freq/positions to existing posting list
     *
     * @param data Posting data to update
     * @param docID Document ID
     * @param freq Term frequency in document
     * @param positions Term positions (appended after freq if non-empty)
     */
    void appendToPostingList(PostingData& data, int docID, int freq,
                             const std::vector<int>& positions);
};

}  // namespace index
}  // namespace diagon
