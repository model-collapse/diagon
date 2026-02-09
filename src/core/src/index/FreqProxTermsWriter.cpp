// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FreqProxTermsWriter.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace diagon {
namespace index {

// ==================== FreqProxTermsWriter ====================

FreqProxTermsWriter::FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder,
                                         size_t expectedTerms)
    : fieldInfosBuilder_(fieldInfosBuilder) {
    // Pre-size term dictionary to avoid rehashing during indexing
    // This significantly reduces malloc overhead
    termToPosting_.reserve(expectedTerms);
}

void FreqProxTermsWriter::addDocument(const document::Document& doc, int docID) {

    // Iterate over all fields in document
    for (const auto& field : doc.getFields()) {
        const std::string& fieldName = field->name();
        const auto& fieldType = field->fieldType();

        // Ensure field exists (get or create)
        fieldInfosBuilder_.getOrAdd(fieldName);

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

        // Track field length for norm computation
        fieldLengths_[fieldName][docID] = static_cast<int>(tokens.size());

        // Count term frequencies using cached map (avoid allocation per document)
        termFreqsCache_.clear();  // Reuse existing map
        for (const auto& token : tokens) {
            termFreqsCache_[token]++;
        }

        // Add each unique term to posting lists with actual frequency
        for (const auto& [term, freq] : termFreqsCache_) {
            addTermOccurrence(fieldName, term, docID, freq, fieldType.indexOptions);
        }
    }
}

void FreqProxTermsWriter::addTermOccurrence(const std::string& fieldName, const std::string& term,
                                            int docID, int freq, IndexOptions indexOptions) {
    // Create composite key: "field\0term" (using null separator to avoid conflicts)
    std::string compositeKey = fieldName + '\0' + term;

    // Check if term already exists
    auto it = termToPosting_.find(compositeKey);

    if (it == termToPosting_.end()) {
        // New term - create posting list with actual frequency
        PostingData data = createPostingList(term, docID, freq);
        termToPosting_[compositeKey] = std::move(data);
        invalidateBytesUsedCache();  // Cache now stale
    } else {
        // Existing term - append to posting list
        PostingData& data = it->second;

        // Skip if same document (duplicate term in same doc)
        if (data.lastDocID == docID) {
            return;
        }

        appendToPostingList(data, docID, freq);
        invalidateBytesUsedCache();  // Cache now stale
    }
}

FreqProxTermsWriter::PostingData FreqProxTermsWriter::createPostingList(const std::string& term,
                                                                        int docID, int freq) {
    PostingData data;

    // Aggressive pre-allocation: typical term has 10-50 postings in Reuters
    // Pre-allocate 100 ints (50 postings = 50 docs) to avoid most reallocations
    // For terms with > 50 postings, vector will still grow but far less frequently
    data.postings.reserve(100);

    // Store initial [docID, freq] pair
    data.postings.push_back(docID);
    data.postings.push_back(freq);

    data.lastDocID = docID;

    return data;
}

void FreqProxTermsWriter::appendToPostingList(PostingData& data, int docID, int freq) {
    // Append [docID, freq] pair
    // Most lists won't exceed 100 ints due to aggressive pre-allocation
    data.postings.push_back(docID);
    data.postings.push_back(freq);

    data.lastDocID = docID;
}

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& term) const {
    // Legacy method - search all fields for this term
    // This is inefficient but maintains backward compatibility for tests
    for (const auto& [compositeKey, data] : termToPosting_) {
        // Extract term from "field\0term" format
        auto nullPos = compositeKey.find('\0');
        if (nullPos != std::string::npos) {
            std::string termPart = compositeKey.substr(nullPos + 1);
            if (termPart == term) {
                return data.postings;
            }
        }
    }
    return {};  // Term not found
}

std::vector<std::string> FreqProxTermsWriter::getTerms() const {
    // Legacy method - return all terms from all fields
    std::set<std::string> uniqueTerms;

    for (const auto& [compositeKey, _] : termToPosting_) {
        // Extract term from "field\0term" format
        auto nullPos = compositeKey.find('\0');
        if (nullPos != std::string::npos) {
            uniqueTerms.insert(compositeKey.substr(nullPos + 1));
        }
    }

    std::vector<std::string> terms(uniqueTerms.begin(), uniqueTerms.end());
    return terms;  // Already sorted by set
}

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& field,
                                                      const std::string& term) const {
    // Create composite key
    std::string compositeKey = field + '\0' + term;

    auto it = termToPosting_.find(compositeKey);
    if (it == termToPosting_.end()) {
        return {};  // Term not found in this field
    }

    return it->second.postings;
}

std::vector<std::string> FreqProxTermsWriter::getTermsForField(const std::string& field) const {
    std::vector<std::string> terms;
    std::string prefix = field + '\0';

    for (const auto& [compositeKey, _] : termToPosting_) {
        // Check if key starts with "field\0"
        if (compositeKey.compare(0, prefix.length(), prefix) == 0) {
            // Extract term part after "field\0"
            std::string term = compositeKey.substr(prefix.length());
            terms.push_back(term);
        }
    }

    // Sort for consistent ordering
    std::sort(terms.begin(), terms.end());

    return terms;
}

int64_t FreqProxTermsWriter::bytesUsed() const {
    // Return cached value if still valid (optimization for frequent calls)
    if (!bytesUsedDirty_) {
        return cachedBytesUsed_;
    }

    // Recalculate memory usage when cache is dirty
    int64_t bytes = termBytePool_.bytesUsed();

    // Posting list vectors (with aggressive pre-allocation, we allocate more than we use)
    for (const auto& [term, data] : termToPosting_) {
        bytes += term.capacity();                         // Term string
        bytes += data.postings.capacity() * sizeof(int);  // Posting vector
    }

    // Map overhead (approximate)
    bytes += termToPosting_.size() * 64;  // Rough estimate

    // Cache the result
    cachedBytesUsed_ = bytes;
    bytesUsedDirty_ = false;

    return bytes;
}

void FreqProxTermsWriter::reset() {
    termBytePool_.reset();
    termToPosting_.clear();
    fieldLengths_.clear();
    invalidateBytesUsedCache();  // Cache now stale
}

void FreqProxTermsWriter::clear() {
    termBytePool_.clear();
    termToPosting_.clear();
    fieldLengths_.clear();
    invalidateBytesUsedCache();  // Cache now stale
}

}  // namespace index
}  // namespace diagon
