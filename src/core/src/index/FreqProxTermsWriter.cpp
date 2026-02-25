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

FreqProxTermsWriter::FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder, size_t expectedTerms)
    : fieldInfosBuilder_(fieldInfosBuilder) {
    // Aggressive pre-sizing to minimize rehashing and malloc overhead
    // Rehashing is expensive: allocate new buckets + move all entries

    // Pre-size term dictionary (main data structure)
    termToPosting_.reserve(expectedTerms);

    // Pre-size field-related containers (typical: 5-20 fields per schema)
    // Reserve extra capacity (1.3x) to avoid rehashing as fields are added
    constexpr size_t EXPECTED_FIELDS = 20;
    fieldStats_.reserve(EXPECTED_FIELDS);
    fieldToSortedTerms_.reserve(EXPECTED_FIELDS);
    fieldNameToId_.reserve(EXPECTED_FIELDS);

    // Pre-size per-document term positions cache (typical: 50-100 unique terms per doc)
    // This is reused across documents, so one-time allocation
    termPositionsCache_.reserve(128);
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
        auto& fieldLengthMap = fieldLengths_[fieldName];
        if (fieldLengthMap.find(docID) == fieldLengthMap.end()) {
            // First time seeing this document for this field
            fieldStats_[fieldName].docCount++;
        }
        fieldLengthMap[docID] = static_cast<int>(tokens.size());

        // Collect term positions using cached map (avoid allocation per document)
        termPositionsCache_.clear();  // Reuse existing map
        for (int pos = 0; pos < static_cast<int>(tokens.size()); pos++) {
            termPositionsCache_[tokens[pos]].push_back(pos);
        }

        // Determine whether to store positions based on indexOptions
        bool storePositions = (fieldType.indexOptions >=
                               IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);

        // Add each unique term to posting lists with actual frequency and positions
        for (const auto& [term, positions] : termPositionsCache_) {
            int freq = static_cast<int>(positions.size());
            addTermOccurrence(fieldName, term, docID, freq,
                              storePositions ? positions : std::vector<int>{},
                              fieldType.indexOptions);
        }
    }
}

void FreqProxTermsWriter::addTermOccurrence(const std::string& fieldName, const std::string& term,
                                            int docID, int freq, const std::vector<int>& positions,
                                            IndexOptions indexOptions) {
    // Get or assign field ID (reduces hash overhead: hash int vs hash string)
    auto fieldIdIt = fieldNameToId_.find(fieldName);
    int fieldId;
    if (fieldIdIt == fieldNameToId_.end()) {
        // New field - assign ID
        fieldId = nextFieldId_++;
        fieldNameToId_[fieldName] = fieldId;
    } else {
        fieldId = fieldIdIt->second;
    }

    // Use (fieldID, term) as key - faster hashing than (fieldName, term)
    auto key = std::make_pair(fieldId, term);

    // Check if term already exists
    auto it = termToPosting_.find(key);

    // Get or create field stats
    FieldStats& stats = fieldStats_[fieldName];

    if (it == termToPosting_.end()) {
        // New term - create posting list with actual frequency and positions
        PostingData data = createPostingList(term, docID, freq, positions);

        // Track memory incrementally: field + term + posting data
        bytesUsed_ += fieldName.capacity() + term.capacity();  // String storage
        bytesUsed_ += data.postings.capacity() * sizeof(int);  // Vector capacity
        bytesUsed_ += 64;  // Hash map node overhead (approximate)

        // Update field statistics incrementally (new term)
        stats.sumTotalTermFreq += freq;  // Add term frequency
        stats.sumDocFreq += 1;           // One document has this term

        // Add term to sorted index (O(log n) insert, eliminates O(n log n) sort during flush)
        fieldToSortedTerms_[fieldName].insert(term);

        termToPosting_[std::move(key)] = std::move(data);
    } else {
        // Existing term - append to posting list
        PostingData& data = it->second;

        // Skip if same document (duplicate term in same doc)
        if (data.lastDocID == docID) {
            return;
        }

        size_t oldCapacity = data.postings.capacity();
        appendToPostingList(data, docID, freq, positions);
        size_t newCapacity = data.postings.capacity();

        // Track delta only if vector grew (reallocation occurred)
        if (newCapacity > oldCapacity) {
            bytesUsed_ += (newCapacity - oldCapacity) * sizeof(int);
        }

        // Update field statistics incrementally (existing term, new document)
        stats.sumTotalTermFreq += freq;  // Add term frequency
        stats.sumDocFreq += 1;           // Another document has this term
    }
}

FreqProxTermsWriter::PostingData
FreqProxTermsWriter::createPostingList(const std::string& term, int docID, int freq,
                                       const std::vector<int>& positions) {
    PostingData data;

    // Aggressive pre-allocation: typical term has 10-50 postings in Reuters
    // Pre-allocate 100 ints to avoid most reallocations
    data.postings.reserve(100);

    // Store initial [docID, freq] pair
    data.postings.push_back(docID);
    data.postings.push_back(freq);

    // Append positions after freq if provided
    for (int pos : positions) {
        data.postings.push_back(pos);
    }

    data.lastDocID = docID;

    return data;
}

void FreqProxTermsWriter::appendToPostingList(PostingData& data, int docID, int freq,
                                              const std::vector<int>& positions) {
    // Append [docID, freq, pos0, ..., posN]
    data.postings.push_back(docID);
    data.postings.push_back(freq);

    // Append positions after freq if provided
    for (int pos : positions) {
        data.postings.push_back(pos);
    }

    data.lastDocID = docID;
}

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& term) const {
    // Legacy method - search all fields for this term
    // This is inefficient but maintains backward compatibility for tests
    for (const auto& [key, data] : termToPosting_) {
        // key is (fieldID, term) pair - check if term matches
        if (key.second == term) {
            return data.postings;
        }
    }
    return {};  // Term not found
}

std::vector<std::string> FreqProxTermsWriter::getTerms() const {
    // Legacy method - return all terms from all fields
    std::set<std::string> uniqueTerms;

    for (const auto& [key, _] : termToPosting_) {
        // key.second is the term
        uniqueTerms.insert(key.second);
    }

    std::vector<std::string> terms(uniqueTerms.begin(), uniqueTerms.end());
    return terms;  // Already sorted by set
}

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& field,
                                                     const std::string& term) const {
    // Get field ID
    auto fieldIdIt = fieldNameToId_.find(field);
    if (fieldIdIt == fieldNameToId_.end()) {
        return {};  // Field not found
    }

    // Use (fieldID, term) as key - faster hashing
    auto key = std::make_pair(fieldIdIt->second, term);

    auto it = termToPosting_.find(key);
    if (it == termToPosting_.end()) {
        return {};  // Term not found in this field
    }

    return it->second.postings;
}

std::vector<std::string> FreqProxTermsWriter::getTermsForField(const std::string& field) const {
    // Use pre-sorted index (eliminates O(n log n) sorting during flush)
    auto it = fieldToSortedTerms_.find(field);
    if (it == fieldToSortedTerms_.end()) {
        return {};  // Field not found
    }

    // Convert set to vector (already sorted)
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

FreqProxTermsWriter::FieldStats
FreqProxTermsWriter::getFieldStats(const std::string& fieldName) const {
    auto it = fieldStats_.find(fieldName);
    if (it != fieldStats_.end()) {
        return it->second;
    }
    // Return default-initialized stats if field not found
    return FieldStats{};
}

int64_t FreqProxTermsWriter::bytesUsed() const {
    // Return incrementally tracked memory usage - O(1) instead of O(n)!
    // No need to scan all posting lists anymore
    return bytesUsed_ + termBytePool_.bytesUsed();
}

void FreqProxTermsWriter::reset() {
    termBytePool_.reset();
    termToPosting_.clear();
    fieldLengths_.clear();
    fieldStats_.clear();
    fieldToSortedTerms_.clear();
    fieldNameToId_.clear();
    nextFieldId_ = 0;
    bytesUsed_ = 0;  // Reset memory counter
}

void FreqProxTermsWriter::clear() {
    termBytePool_.clear();
    termToPosting_.clear();
    fieldLengths_.clear();
    fieldStats_.clear();
    fieldToSortedTerms_.clear();
    fieldNameToId_.clear();
    nextFieldId_ = 0;
    bytesUsed_ = 0;  // Reset memory counter
}

}  // namespace index
}  // namespace diagon
