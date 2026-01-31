// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/index/BatchPostingsEnum.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/Directory.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * SimpleFieldsProducer - Reads .post files created by SimpleFieldsConsumer
 *
 * Phase 4 implementation that loads entire .post file into memory.
 * This is simple but not optimized - production version (Phase 5) will use
 * mmap and lazy loading.
 *
 * File Format (.post):
 * --------------------
 * Magic: 0x504F5354 ("POST")
 * Version: 1
 * NumTerms: N
 * For each term:
 *   TermLength: L
 *   TermBytes: [L bytes]
 *   NumPostings: P
 *   Postings: [docID, freq] * P
 *
 * Thread Safety: Thread-safe for concurrent reads after construction.
 */
class SimpleFieldsProducer : public FieldsProducer {
public:
    /**
     * Posting structure
     */
    struct Posting {
        int docID;
        int freq;

        Posting(int d, int f)
            : docID(d)
            , freq(f) {}
    };

    /**
     * Term data structure
     */
    struct TermData {
        std::string term;
        std::vector<Posting> postings;
    };

    /**
     * Constructor - reads .post file into memory
     *
     * @param dir Directory containing index
     * @param segmentName Segment name (e.g., "_0")
     * @param fieldName Field name
     */
    SimpleFieldsProducer(store::Directory& dir, const std::string& segmentName,
                         const std::string& fieldName);

    /**
     * Get terms for a field (FieldsProducer interface)
     *
     * @param field Field name (must match the field this producer was created for)
     * @return Terms instance, or nullptr if field doesn't match
     */
    std::unique_ptr<::diagon::index::Terms> terms(const std::string& field) override;

    /**
     * Get all term data (for testing/debugging)
     */
    const std::vector<TermData>& getTermData() const { return terms_; }

    /**
     * Get number of terms
     */
    size_t size() const { return terms_.size(); }

    /**
     * Check integrity (FieldsProducer interface)
     */
    void checkIntegrity() override {
        // Phase 4: No checksum validation yet
    }

    /**
     * Close (FieldsProducer interface)
     */
    void close() override {
        // Nothing to close - data already in memory
    }

private:
    std::string segmentName_;
    std::string fieldName_;
    std::vector<TermData> terms_;  // Sorted by term

    /**
     * Load .post file into memory
     */
    void load(store::Directory& dir);

    /**
     * Get postings filename
     */
    std::string getPostingsFileName() const;
};

/**
 * SimpleTerms - Terms implementation for SimpleFieldsProducer
 */
class SimpleTerms : public ::diagon::index::Terms {
public:
    explicit SimpleTerms(const std::vector<SimpleFieldsProducer::TermData>& terms)
        : terms_(terms) {}

    std::unique_ptr<::diagon::index::TermsEnum> iterator() const override;

    int64_t size() const override { return static_cast<int64_t>(terms_.size()); }

private:
    const std::vector<SimpleFieldsProducer::TermData>& terms_;
};

/**
 * SimpleTermsEnum - TermsEnum implementation for SimpleFieldsProducer
 */
class SimpleTermsEnum : public ::diagon::index::TermsEnum {
public:
    explicit SimpleTermsEnum(const std::vector<SimpleFieldsProducer::TermData>& terms)
        : terms_(terms)
        , current_(-1) {}

    bool next() override;

    bool seekExact(const util::BytesRef& text) override;

    SeekStatus seekCeil(const util::BytesRef& text) override;

    util::BytesRef term() const override;

    int docFreq() const override;

    int64_t totalTermFreq() const override;

    std::unique_ptr<::diagon::index::PostingsEnum> postings() override;

    std::unique_ptr<::diagon::index::PostingsEnum> postings(bool useBatch) override;

private:
    const std::vector<SimpleFieldsProducer::TermData>& terms_;
    int current_;  // Current term index (-1 = before first)

    /**
     * Check if current position is valid
     */
    bool isValid() const { return current_ >= 0 && current_ < static_cast<int>(terms_.size()); }
};

/**
 * SimplePostingsEnum - PostingsEnum implementation for SimpleFieldsProducer
 */
class SimplePostingsEnum : public ::diagon::index::PostingsEnum {
public:
    explicit SimplePostingsEnum(const std::vector<SimpleFieldsProducer::Posting>& postings)
        : postings_(postings)
        , current_(-1) {}

    int nextDoc() override;

    int advance(int target) override;

    int docID() const override;

    int64_t cost() const override { return static_cast<int64_t>(postings_.size()); }

    int freq() const override;

private:
    const std::vector<SimpleFieldsProducer::Posting>& postings_;
    int current_;  // Current posting index (-1 = before first)

    /**
     * Check if current position is valid
     */
    bool isValid() const { return current_ >= 0 && current_ < static_cast<int>(postings_.size()); }
};

/**
 * SimpleBatchPostingsEnum - Batch-capable postings enum for in-memory postings
 *
 * Quick validation implementation: wraps std::vector<Posting> with batch interface
 * to eliminate virtual call overhead in BatchTermScorer.
 */
class SimpleBatchPostingsEnum : public ::diagon::index::BatchPostingsEnum {
public:
    explicit SimpleBatchPostingsEnum(const std::vector<SimpleFieldsProducer::Posting>& postings)
        : postings_(postings)
        , current_(-1) {}

    // ==================== BatchPostingsEnum ====================

    int nextBatch(::diagon::index::PostingsBatch& batch) override;

    // ==================== DocIdSetIterator ====================

    int nextDoc() override;

    int advance(int target) override;

    int docID() const override;

    int64_t cost() const override { return static_cast<int64_t>(postings_.size()); }

    // ==================== PostingsEnum ====================

    int freq() const override;

private:
    const std::vector<SimpleFieldsProducer::Posting>& postings_;
    int current_;  // Current posting index (-1 = before first)

    bool isValid() const { return current_ >= 0 && current_ < static_cast<int>(postings_.size()); }
};

}  // namespace codecs
}  // namespace diagon
