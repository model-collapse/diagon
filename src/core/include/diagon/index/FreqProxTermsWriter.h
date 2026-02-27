// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/util/ByteBlockPool.h"
#include "diagon/util/IntBlockPool.h"

#include <algorithm>
#include <functional>
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
 * Data structure layout (optimized for indexing throughput):
 *   - Per-field posting maps: fieldPostings_[fieldId][term] = PostingData
 *   - Field metadata in flat vectors indexed by field ID (no string hashing)
 *   - Sorted terms computed lazily at flush time (no std::set during indexing)
 *
 * Thread Safety: NOT thread-safe (per-thread instance in DWPT)
 */
class FreqProxTermsWriter {
public:
    explicit FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder,
                                 size_t expectedTerms = 10000);

    ~FreqProxTermsWriter() = default;

    // Disable copy/move
    FreqProxTermsWriter(const FreqProxTermsWriter&) = delete;
    FreqProxTermsWriter& operator=(const FreqProxTermsWriter&) = delete;
    FreqProxTermsWriter(FreqProxTermsWriter&&) = delete;
    FreqProxTermsWriter& operator=(FreqProxTermsWriter&&) = delete;

    /**
     * Add document to in-memory posting lists
     */
    void addDocument(const document::Document& doc, int docID);

    /**
     * Add a single indexed field to in-memory posting lists
     * Used by DWPT's single-pass field processing.
     */
    void addField(const document::IndexableField& field, int docID);

    /**
     * Get bytes used (approximate)
     */
    int64_t bytesUsed() const;

    /**
     * Get posting list for term (legacy, for testing)
     */
    std::vector<int> getPostingList(const std::string& term) const;

    /**
     * Get all terms (legacy, for testing)
     */
    std::vector<std::string> getTerms() const;

    /**
     * Get posting list for field-specific term
     */
    std::vector<int> getPostingList(const std::string& field, const std::string& term) const;

    /**
     * Get all terms for a specific field (sorted)
     * Sorting deferred to call time — no std::set maintained during indexing.
     */
    std::vector<std::string> getTermsForField(const std::string& field) const;

    /**
     * Posting list data
     */
    struct PostingData {
        int lastDocID = -1;
        int pendingFreqIndex = -1;  // Index of freq slot in postings (for in-place update)
        std::vector<int> postings;  // [docID, freq, pos..., docID, freq, pos..., ...]
    };

    /**
     * Field statistics for Terms implementation
     */
    struct FieldStats {
        int64_t sumTotalTermFreq = 0;
        int64_t sumDocFreq = 0;
        int docCount = 0;
    };

    /**
     * Get field statistics by name
     */
    FieldStats getFieldStats(const std::string& fieldName) const;

    /**
     * Per-field document lengths for norm computation.
     * Flat vector keyed by docID — O(1) access, no hashing.
     */
    struct FieldLengthData {
        std::vector<int> lengths;

        void set(int docID, int length) {
            if (docID >= static_cast<int>(lengths.size())) {
                lengths.resize(docID + 1, 0);
            }
            lengths[docID] = length;
        }

        int get(int docID) const {
            if (docID < static_cast<int>(lengths.size())) {
                return lengths[docID];
            }
            return 0;
        }

        bool has(int docID) const {
            return docID < static_cast<int>(lengths.size()) && lengths[docID] != 0;
        }
    };

    /**
     * Get field name for a given field ID
     */
    const std::string& getFieldName(int fieldId) const {
        return idToFieldName_[fieldId];
    }

    /**
     * Get number of registered fields
     */
    int getFieldCount() const { return nextFieldId_; }

    /**
     * Get field lengths by field ID (for flush)
     */
    const FieldLengthData& getFieldLengthData(int fieldId) const {
        return fieldLengths_[fieldId];
    }

    /**
     * Get field stats by field ID
     */
    const FieldStats& getFieldStatsById(int fieldId) const {
        return fieldStats_[fieldId];
    }

    /**
     * Get per-field posting map by field ID (for flush, getPostingList, etc.)
     */
    const std::unordered_map<std::string, PostingData>& getFieldPostings(int fieldId) const {
        return fieldPostings_[fieldId];
    }

    /**
     * Get field ID by name, or -1 if not found
     */
    int getFieldId(const std::string& fieldName) const {
        auto it = fieldNameToId_.find(fieldName);
        return it != fieldNameToId_.end() ? it->second : -1;
    }

    /**
     * Get field lengths for norm computation (backward-compatible interface for DWPT flush)
     * Returns pairs of (fieldName, fieldLengthData) for all fields.
     */
    void forEachFieldLength(
        const std::function<void(const std::string& fieldName, const FieldLengthData& data)>& fn)
        const {
        for (int i = 0; i < nextFieldId_; i++) {
            fn(idToFieldName_[i], fieldLengths_[i]);
        }
    }

    void reset();
    void clear();

private:
    // Field name <-> ID mapping
    std::unordered_map<std::string, int> fieldNameToId_;
    std::vector<std::string> idToFieldName_;  // reverse mapping
    int nextFieldId_ = 0;

    // Resolve or assign field ID. Returns field ID.
    int resolveFieldId(const std::string& fieldName);

    // Ensure per-field vectors are sized for fieldId
    void ensureFieldCapacity(int fieldId);

    // === Per-field posting maps ===
    // fieldPostings_[fieldId][term] = PostingData
    // Eliminates pair key hashing — just string hash per term lookup.
    std::vector<std::unordered_map<std::string, PostingData>> fieldPostings_;

    // === Field metadata in flat vectors (indexed by field ID) ===
    std::vector<FieldLengthData> fieldLengths_;
    std::vector<FieldStats> fieldStats_;

    // Field metadata tracker (reference)
    FieldInfosBuilder& fieldInfosBuilder_;

    // Term byte storage
    util::ByteBlockPool termBytePool_;

    // Incremental memory usage tracking
    int64_t bytesUsed_ = 0;

    // Pre-sizing hint
    size_t expectedTermsPerField_;

};

}  // namespace index
}  // namespace diagon
