// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/Fields.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/FreqProxTermsWriter.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace index {

// Forward declaration
class FieldInfos;

/**
 * FreqProxFields - Fields implementation that wraps FreqProxTermsWriter
 *
 * Provides streaming "pull" API over in-memory posting lists.
 * Used during segment flush to allow codec to iterate over fields/terms.
 *
 * Architecture:
 * - FreqProxFields: Iterates over fields
 * - FreqProxTerms: Iterates over terms for a field
 * - FreqProxTermsEnum: Iterates over terms and provides postings
 */
class FreqProxFields : public Fields {
public:
    /**
     * Constructor
     *
     * @param termsWriter In-memory terms writer to wrap
     * @param fieldInfos Field metadata to determine which fields to expose
     */
    explicit FreqProxFields(const FreqProxTermsWriter& termsWriter,
                           const FieldInfos& fieldInfos);

    // ==================== Fields Interface ====================

    std::unique_ptr<Terms> terms(const std::string& field) override;

    int size() const override;

    std::unique_ptr<Fields::Iterator> iterator() override;

private:
    const FreqProxTermsWriter& termsWriter_;

    // Pre-computed field list (currently only "_all" field)
    std::vector<std::string> fields_;

    // Fields iterator implementation
    class FieldsIterator : public Fields::Iterator {
    public:
        explicit FieldsIterator(const std::vector<std::string>& fields)
            : fields_(fields), position_(0) {}

        bool hasNext() const override {
            return position_ < fields_.size();
        }

        std::string next() override {
            if (!hasNext()) {
                throw std::runtime_error("No more fields");
            }
            return fields_[position_++];
        }

    private:
        const std::vector<std::string>& fields_;
        size_t position_;
    };
};

/**
 * FreqProxTerms - Terms implementation for a single field
 *
 * Provides iterator over all terms in the field.
 */
class FreqProxTerms : public Terms {
public:
    /**
     * Constructor
     *
     * @param fieldName Field name
     * @param termsWriter Terms writer containing postings
     */
    FreqProxTerms(const std::string& fieldName,
                  const FreqProxTermsWriter& termsWriter);

    // ==================== Terms Interface ====================

    std::unique_ptr<TermsEnum> iterator() const override;

    int64_t size() const override;

    int getDocCount() const override;

    int64_t getSumTotalTermFreq() const override;

    int64_t getSumDocFreq() const override;

private:
    std::string fieldName_;
    const FreqProxTermsWriter& termsWriter_;

    // Pre-computed sorted term list
    std::vector<std::string> sortedTerms_;

    // Statistics
    int64_t sumTotalTermFreq_;
    int64_t sumDocFreq_;
    int docCount_;
};

/**
 * FreqProxTermsEnum - TermsEnum implementation that iterates over terms
 *
 * Provides access to term text and posting lists for each term.
 */
class FreqProxTermsEnum : public TermsEnum {
public:
    /**
     * Constructor
     *
     * @param fieldName Field name for this terms enum
     * @param sortedTerms List of terms in sorted order
     * @param termsWriter Terms writer containing postings
     */
    FreqProxTermsEnum(const std::string& fieldName,
                      const std::vector<std::string>& sortedTerms,
                      const FreqProxTermsWriter& termsWriter);

    // ==================== TermsEnum Interface ====================

    bool next() override;

    bool seekExact(const util::BytesRef& text) override;

    SeekStatus seekCeil(const util::BytesRef& text) override;

    util::BytesRef term() const override;

    int docFreq() const override;

    int64_t totalTermFreq() const override;

    std::unique_ptr<PostingsEnum> postings() override;

    std::unique_ptr<PostingsEnum> postings(bool useBatch) override;

private:
    std::string fieldName_;
    const std::vector<std::string>& sortedTerms_;
    const FreqProxTermsWriter& termsWriter_;

    // Current position in term iteration
    int64_t termOrd_;  // Current term ordinal (-1 = before first term)
    std::string currentTerm_;

    // Current posting list for current term
    std::vector<int> currentPostings_;  // [docID, freq, docID, freq, ...]
    int currentDocFreq_;
    int64_t currentTotalTermFreq_;

    // Load posting list for current term
    void loadCurrentPostings();

    // PostingsEnum implementation that wraps posting list
    class FreqProxPostingsEnum : public PostingsEnum {
    public:
        FreqProxPostingsEnum(const std::vector<int>& postings)
            : postings_(postings), position_(-1), currentDoc_(-1), currentFreq_(1) {}

        // DocIdSetIterator
        int docID() const override { return currentDoc_; }

        int nextDoc() override {
            position_++;
            if (position_ >= static_cast<int>(postings_.size())) {
                currentDoc_ = NO_MORE_DOCS;
                return NO_MORE_DOCS;
            }

            currentDoc_ = postings_[position_];
            currentFreq_ = (position_ + 1 < static_cast<int>(postings_.size()))
                ? postings_[position_ + 1]
                : 1;
            position_++;  // Skip freq

            return currentDoc_;
        }

        int advance(int target) override {
            while (currentDoc_ < target && currentDoc_ != NO_MORE_DOCS) {
                nextDoc();
            }
            return currentDoc_;
        }

        int64_t cost() const override {
            return postings_.size() / 2;  // Number of docs (postings has [doc, freq] pairs)
        }

        // PostingsEnum
        int freq() const override { return currentFreq_; }

    private:
        const std::vector<int>& postings_;
        int position_;
        int currentDoc_;
        int currentFreq_;
    };
};

}  // namespace index
}  // namespace diagon
