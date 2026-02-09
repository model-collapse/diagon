// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FreqProxFields.h"
#include "diagon/index/FieldInfo.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace diagon {
namespace index {

// ==================== FreqProxFields Implementation ====================

FreqProxFields::FreqProxFields(const FreqProxTermsWriter& termsWriter,
                               const FieldInfos& fieldInfos)
    : termsWriter_(termsWriter) {

    // Iterate over actual fields from FieldInfos
    for (const auto& fieldInfo : fieldInfos) {

        // Only include indexed fields
        if (fieldInfo.indexOptions != IndexOptions::NONE) {
            fields_.push_back(fieldInfo.name);
        }
    }

}

std::unique_ptr<Terms> FreqProxFields::terms(const std::string& field) {

    // Check if field exists
    auto it = std::find(fields_.begin(), fields_.end(), field);
    if (it == fields_.end()) {
        return nullptr;  // Field not found
    }

    return std::make_unique<FreqProxTerms>(field, termsWriter_);
}

int FreqProxFields::size() const {
    return static_cast<int>(fields_.size());
}

std::unique_ptr<Fields::Iterator> FreqProxFields::iterator() {
    return std::make_unique<FieldsIterator>(fields_);
}

// ==================== FreqProxTerms Implementation ====================

FreqProxTerms::FreqProxTerms(const std::string& fieldName,
                             const FreqProxTermsWriter& termsWriter)
    : fieldName_(fieldName)
    , termsWriter_(termsWriter)
    , sumTotalTermFreq_(0)
    , sumDocFreq_(0)
    , docCount_(0) {

    // Get terms for this specific field only
    sortedTerms_ = termsWriter_.getTermsForField(fieldName);

    // Use pre-computed statistics (eliminates flush overhead!)
    // Statistics are computed incrementally during indexing, not by scanning posting lists
    auto stats = termsWriter_.getFieldStats(fieldName);
    sumTotalTermFreq_ = stats.sumTotalTermFreq;
    sumDocFreq_ = stats.sumDocFreq;
    docCount_ = stats.docCount;
}

std::unique_ptr<TermsEnum> FreqProxTerms::iterator() const {
    return std::make_unique<FreqProxTermsEnum>(fieldName_, sortedTerms_, termsWriter_);
}

int64_t FreqProxTerms::size() const {
    return static_cast<int64_t>(sortedTerms_.size());
}

int FreqProxTerms::getDocCount() const {
    return docCount_;
}

int64_t FreqProxTerms::getSumTotalTermFreq() const {
    return sumTotalTermFreq_;
}

int64_t FreqProxTerms::getSumDocFreq() const {
    return sumDocFreq_;
}

// ==================== FreqProxTermsEnum Implementation ====================

FreqProxTermsEnum::FreqProxTermsEnum(const std::string& fieldName,
                                     const std::vector<std::string>& sortedTerms,
                                     const FreqProxTermsWriter& termsWriter)
    : fieldName_(fieldName)
    , sortedTerms_(sortedTerms)
    , termsWriter_(termsWriter)
    , termOrd_(-1)  // Before first term
    , currentDocFreq_(0)
    , currentTotalTermFreq_(0) {
}

bool FreqProxTermsEnum::next() {
    termOrd_++;

    if (termOrd_ >= static_cast<int64_t>(sortedTerms_.size())) {
        return false;  // No more terms
    }

    // Update current term
    currentTerm_ = sortedTerms_[static_cast<size_t>(termOrd_)];

    // Load postings for this term
    loadCurrentPostings();

    return true;
}

bool FreqProxTermsEnum::seekExact(const util::BytesRef& text) {
    std::string target(reinterpret_cast<const char*>(text.data()), text.length());
    auto it = std::find(sortedTerms_.begin(), sortedTerms_.end(), target);
    if (it != sortedTerms_.end()) {
        termOrd_ = std::distance(sortedTerms_.begin(), it);
        currentTerm_ = *it;
        loadCurrentPostings();
        return true;
    }
    return false;
}

TermsEnum::SeekStatus FreqProxTermsEnum::seekCeil(const util::BytesRef& text) {
    std::string target(reinterpret_cast<const char*>(text.data()), text.length());
    auto it = std::lower_bound(sortedTerms_.begin(), sortedTerms_.end(), target);
    if (it == sortedTerms_.end()) {
        return SeekStatus::END;
    }
    termOrd_ = std::distance(sortedTerms_.begin(), it);
    currentTerm_ = *it;
    loadCurrentPostings();
    return (*it == target) ? SeekStatus::FOUND : SeekStatus::NOT_FOUND;
}

util::BytesRef FreqProxTermsEnum::term() const {
    if (termOrd_ < 0) {
        throw std::runtime_error("No current term (call next() first)");
    }
    return util::BytesRef(reinterpret_cast<const uint8_t*>(currentTerm_.data()),
                         currentTerm_.size());
}

int FreqProxTermsEnum::docFreq() const {
    return currentDocFreq_;
}

int64_t FreqProxTermsEnum::totalTermFreq() const {
    return currentTotalTermFreq_;
}

std::unique_ptr<PostingsEnum> FreqProxTermsEnum::postings() {
    if (termOrd_ < 0) {
        throw std::runtime_error("No current term (call next() first)");
    }

    return std::make_unique<FreqProxPostingsEnum>(currentPostings_);
}

std::unique_ptr<PostingsEnum> FreqProxTermsEnum::postings(bool useBatch) {
    // For now, ignore useBatch and use simple implementation
    (void)useBatch;
    return postings();
}

void FreqProxTermsEnum::loadCurrentPostings() {
    // Get posting list from terms writer (field-specific)
    currentPostings_ = termsWriter_.getPostingList(fieldName_, currentTerm_);

    // Compute statistics
    // postings format: [docID, freq, docID, freq, ...]
    currentDocFreq_ = 0;
    currentTotalTermFreq_ = 0;

    for (size_t i = 0; i < currentPostings_.size(); i += 2) {
        int freq = (i + 1 < currentPostings_.size()) ? currentPostings_[i + 1] : 1;
        currentTotalTermFreq_ += freq;
        currentDocFreq_++;
    }
}

}  // namespace index
}  // namespace diagon
