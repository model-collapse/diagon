// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SimpleFieldsProducer.h"

#include "diagon/store/IOContext.h"
#include "diagon/store/IndexInput.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>

namespace diagon {
namespace codecs {

using namespace diagon::store;
using namespace diagon::index;
using namespace diagon::util;

// Magic number: "POST" in ASCII
static const int32_t MAGIC = 0x504F5354;
static const int32_t VERSION = 1;

// ==================== SimpleFieldsProducer ====================

SimpleFieldsProducer::SimpleFieldsProducer(Directory& dir, const std::string& segmentName,
                                           const std::string& fieldName)
    : segmentName_(segmentName)
    , fieldName_(fieldName) {
    load(dir);
}

std::unique_ptr<Terms> SimpleFieldsProducer::terms(const std::string& field) {
    // Check if field matches
    if (field != fieldName_) {
        return nullptr;  // This producer only handles one field
    }
    return std::make_unique<SimpleTerms>(terms_);
}

std::string SimpleFieldsProducer::getPostingsFileName() const {
    return segmentName_ + ".post";
}

void SimpleFieldsProducer::load(Directory& dir) {
    std::string fileName = getPostingsFileName();

    // Open input
    auto input = dir.openInput(fileName, IOContext::DEFAULT);

    // Read and validate magic
    int32_t magic = input->readInt();
    if (magic != MAGIC) {
        throw IOException("Invalid .post file magic: " + std::to_string(magic));
    }

    // Read and validate version
    int32_t version = input->readInt();
    if (version != VERSION) {
        throw IOException("Unsupported .post file version: " + std::to_string(version));
    }

    // Read number of terms
    int32_t numTerms = input->readInt();
    terms_.reserve(numTerms);

    // Read each term
    for (int32_t i = 0; i < numTerms; i++) {
        TermData termData;

        // Read term
        termData.term = input->readString();

        // Read postings
        int32_t numPostings = input->readInt();
        termData.postings.reserve(numPostings);

        for (int32_t j = 0; j < numPostings; j++) {
            int32_t docID = input->readInt();
            int32_t freq = input->readInt();
            termData.postings.emplace_back(docID, freq);
        }

        terms_.push_back(std::move(termData));
    }

    // Terms should already be sorted from writer, but verify
    // (SimpleFieldsConsumer writes in sorted order)
}

// ==================== SimpleTerms ====================

std::unique_ptr<TermsEnum> SimpleTerms::iterator() const {
    return std::make_unique<SimpleTermsEnum>(terms_);
}

// ==================== SimpleTermsEnum ====================

bool SimpleTermsEnum::next() {
    current_++;
    return current_ < static_cast<int>(terms_.size());
}

bool SimpleTermsEnum::seekExact(const BytesRef& text) {
    std::string target(reinterpret_cast<const char*>(text.bytes().data()), text.length());

    // Binary search
    auto it = std::lower_bound(terms_.begin(), terms_.end(), target,
                               [](const SimpleFieldsProducer::TermData& term,
                                  const std::string& value) { return term.term < value; });

    if (it != terms_.end() && it->term == target) {
        current_ = static_cast<int>(std::distance(terms_.begin(), it));
        return true;
    }

    return false;
}

TermsEnum::SeekStatus SimpleTermsEnum::seekCeil(const BytesRef& text) {
    std::string target(reinterpret_cast<const char*>(text.bytes().data()), text.length());

    // Binary search for ceiling
    auto it = std::lower_bound(terms_.begin(), terms_.end(), target,
                               [](const SimpleFieldsProducer::TermData& term,
                                  const std::string& value) { return term.term < value; });

    if (it == terms_.end()) {
        // Past last term
        return SeekStatus::END;
    }

    current_ = static_cast<int>(std::distance(terms_.begin(), it));

    if (it->term == target) {
        return SeekStatus::FOUND;
    } else {
        return SeekStatus::NOT_FOUND;
    }
}

BytesRef SimpleTermsEnum::term() const {
    if (!isValid()) {
        return BytesRef();
    }

    const std::string& termStr = terms_[current_].term;
    return BytesRef(reinterpret_cast<const uint8_t*>(termStr.data()),
                    static_cast<int>(termStr.length()));
}

int SimpleTermsEnum::docFreq() const {
    if (!isValid()) {
        return 0;
    }
    return static_cast<int>(terms_[current_].postings.size());
}

int64_t SimpleTermsEnum::totalTermFreq() const {
    if (!isValid()) {
        return 0;
    }

    // Sum all frequencies
    int64_t total = 0;
    for (const auto& posting : terms_[current_].postings) {
        total += posting.freq;
    }
    return total;
}

std::unique_ptr<PostingsEnum> SimpleTermsEnum::postings() {
    if (!isValid()) {
        return nullptr;
    }

    return std::make_unique<SimplePostingsEnum>(terms_[current_].postings);
}

std::unique_ptr<PostingsEnum> SimpleTermsEnum::postings(bool useBatch) {
    if (!isValid()) {
        return nullptr;
    }

    if (useBatch) {
        // Return batch-capable implementation
        return std::make_unique<SimpleBatchPostingsEnum>(terms_[current_].postings);
    } else {
        // Return regular implementation
        return std::make_unique<SimplePostingsEnum>(terms_[current_].postings);
    }
}

// ==================== SimplePostingsEnum ====================

int SimplePostingsEnum::nextDoc() {
    current_++;
    if (current_ >= static_cast<int>(postings_.size())) {
        return NO_MORE_DOCS;
    }
    return postings_[current_].docID;
}

int SimplePostingsEnum::advance(int target) {
    // Linear scan for simplicity (Phase 4)
    // Phase 5 will add skip lists for fast advance
    while (current_ + 1 < static_cast<int>(postings_.size())) {
        current_++;
        if (postings_[current_].docID >= target) {
            return postings_[current_].docID;
        }
    }

    current_ = static_cast<int>(postings_.size());
    return NO_MORE_DOCS;
}

int SimplePostingsEnum::docID() const {
    if (!isValid()) {
        return -1;
    }
    return postings_[current_].docID;
}

int SimplePostingsEnum::freq() const {
    if (!isValid()) {
        return 0;
    }
    return postings_[current_].freq;
}

// ==================== SimpleBatchPostingsEnum ====================

int SimpleBatchPostingsEnum::nextBatch(index::PostingsBatch& batch) {
    int count = 0;

    // Fill batch with up to batch.capacity documents
    while (count < batch.capacity && current_ + 1 < static_cast<int>(postings_.size())) {
        current_++;
        batch.docs[count] = postings_[current_].docID;
        batch.freqs[count] = postings_[current_].freq;
        count++;
    }

    batch.count = count;
    return count;
}

int SimpleBatchPostingsEnum::nextDoc() {
    current_++;
    if (current_ >= static_cast<int>(postings_.size())) {
        return NO_MORE_DOCS;
    }
    return postings_[current_].docID;
}

int SimpleBatchPostingsEnum::advance(int target) {
    // Linear scan for simplicity
    while (current_ + 1 < static_cast<int>(postings_.size())) {
        current_++;
        if (postings_[current_].docID >= target) {
            return postings_[current_].docID;
        }
    }

    current_ = static_cast<int>(postings_.size());
    return NO_MORE_DOCS;
}

int SimpleBatchPostingsEnum::docID() const {
    if (!isValid()) {
        return -1;
    }
    return postings_[current_].docID;
}

int SimpleBatchPostingsEnum::freq() const {
    if (!isValid()) {
        return 0;
    }
    return postings_[current_].freq;
}

}  // namespace codecs
}  // namespace diagon
