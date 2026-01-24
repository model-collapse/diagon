// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FreqProxTermsWriter.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace index {

// ==================== FreqProxTermsWriter ====================

FreqProxTermsWriter::FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder)
    : fieldInfosBuilder_(fieldInfosBuilder) {}

void FreqProxTermsWriter::addDocument(const document::Document& doc, int docID) {
    // Iterate over all fields in document
    for (const auto& field : doc.getFields()) {
        const std::string& fieldName = field->name();
        const auto& fieldType = field->fieldType();

        // Update field metadata
        fieldInfosBuilder_.updateIndexOptions(fieldName, fieldType.indexOptions);
        fieldInfosBuilder_.updateDocValuesType(fieldName, fieldType.docValuesType);

        // Skip non-indexed fields
        if (fieldType.indexOptions == IndexOptions::NONE) {
            continue;
        }

        // Get field value
        auto value = field->stringValue();
        if (!value.has_value()) {
            continue;  // Skip null values
        }

        // Tokenize field
        std::vector<std::string> tokens = field->tokenize();

        // Count term frequencies
        std::unordered_map<std::string, int> termFreqs;
        for (const auto& token : tokens) {
            termFreqs[token]++;
        }

        // Add each unique term to posting lists
        for (const auto& [term, freq] : termFreqs) {
            addTermOccurrence(fieldName, term, docID, fieldType.indexOptions);
        }
    }
}

void FreqProxTermsWriter::addTermOccurrence(const std::string& fieldName, const std::string& term,
                                            int docID, IndexOptions indexOptions) {
    // Check if term already exists
    auto it = termToPosting_.find(term);

    if (it == termToPosting_.end()) {
        // New term - create posting list
        PostingData data = createPostingList(term, docID);
        termToPosting_[term] = std::move(data);
    } else {
        // Existing term - append to posting list
        PostingData& data = it->second;

        // Skip if same document (duplicate term in same doc)
        if (data.lastDocID == docID) {
            return;
        }

        appendToPostingList(data, docID);
    }
}

FreqProxTermsWriter::PostingData FreqProxTermsWriter::createPostingList(const std::string& term,
                                                                        int docID) {
    PostingData data;

    // Store initial [docID, freq] pair
    data.postings.push_back(docID);
    data.postings.push_back(1);  // freq = 1

    data.lastDocID = docID;

    return data;
}

void FreqProxTermsWriter::appendToPostingList(PostingData& data, int docID) {
    // Append [docID, freq] pair
    data.postings.push_back(docID);
    data.postings.push_back(1);  // freq = 1

    data.lastDocID = docID;
}

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& term) const {
    auto it = termToPosting_.find(term);
    if (it == termToPosting_.end()) {
        return {};  // Term not found
    }

    return it->second.postings;
}

std::vector<std::string> FreqProxTermsWriter::getTerms() const {
    std::vector<std::string> terms;
    terms.reserve(termToPosting_.size());

    for (const auto& [term, _] : termToPosting_) {
        terms.push_back(term);
    }

    // Sort for consistent ordering
    std::sort(terms.begin(), terms.end());

    return terms;
}

int64_t FreqProxTermsWriter::bytesUsed() const {
    // ByteBlockPool memory
    int64_t bytes = termBytePool_.bytesUsed();

    // Posting list vectors (approximate)
    for (const auto& [term, data] : termToPosting_) {
        bytes += term.capacity();                         // Term string
        bytes += data.postings.capacity() * sizeof(int);  // Posting vector
    }

    // Map overhead (approximate)
    bytes += termToPosting_.size() * 64;  // Rough estimate

    return bytes;
}

void FreqProxTermsWriter::reset() {
    termBytePool_.reset();
    termToPosting_.clear();
}

void FreqProxTermsWriter::clear() {
    termBytePool_.clear();
    termToPosting_.clear();
}

}  // namespace index
}  // namespace diagon
