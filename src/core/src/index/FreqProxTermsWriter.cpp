// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FreqProxTermsWriter.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace diagon {
namespace index {

// ==================== FreqProxTermsWriter ====================

FreqProxTermsWriter::FreqProxTermsWriter(FieldInfosBuilder& fieldInfosBuilder, size_t expectedTerms)
    : fieldInfosBuilder_(fieldInfosBuilder)
    , expectedTermsPerField_(expectedTerms / 4) {
    // Pre-size field-related containers
    constexpr size_t EXPECTED_FIELDS = 32;
    fieldNameToId_.reserve(EXPECTED_FIELDS);
    idToFieldName_.reserve(EXPECTED_FIELDS);
    fieldPostings_.reserve(EXPECTED_FIELDS);
    fieldLengths_.reserve(EXPECTED_FIELDS);
    fieldStats_.reserve(EXPECTED_FIELDS);
}

int FreqProxTermsWriter::resolveFieldId(const std::string& fieldName) {
    auto it = fieldNameToId_.find(fieldName);
    if (it != fieldNameToId_.end()) {
        return it->second;
    }
    int id = nextFieldId_++;
    fieldNameToId_[fieldName] = id;
    idToFieldName_.push_back(fieldName);
    ensureFieldCapacity(id);
    return id;
}

void FreqProxTermsWriter::ensureFieldCapacity(int fieldId) {
    size_t needed = static_cast<size_t>(fieldId) + 1;
    if (fieldPostings_.size() < needed) {
        fieldPostings_.resize(needed);
        fieldPostings_.back().reserve(expectedTermsPerField_);
        fieldLengths_.resize(needed);
        fieldStats_.resize(needed);
    }
}

void FreqProxTermsWriter::addDocument(const document::Document& doc, int docID) {
    for (const auto& field : doc.getFields()) {
        addField(*field, docID);
    }
}

void FreqProxTermsWriter::addField(const document::IndexableField& field, int docID) {
    const std::string& fieldName = field.name();
    const auto& fieldType = field.fieldType();

    fieldInfosBuilder_.getOrAdd(fieldName);
    fieldInfosBuilder_.updateIndexOptions(fieldName, fieldType.indexOptions);
    fieldInfosBuilder_.updateDocValuesType(fieldName, fieldType.docValuesType);

    if (fieldType.indexOptions == IndexOptions::NONE) {
        return;
    }

    auto value = field.stringValue();
    if (!value.has_value()) {
        return;
    }

    // Resolve field ID once
    int fieldId = resolveFieldId(fieldName);

    std::vector<std::string> tokens = field.tokenize();

    // Track field length (flat vector, O(1))
    auto& fld = fieldLengths_[fieldId];
    if (!fld.has(docID)) {
        fieldStats_[fieldId].docCount++;
    }
    fld.set(docID, static_cast<int>(tokens.size()));

    bool storePositions = (fieldType.indexOptions >=
                           IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);

    // Direct-to-posting token processing: eliminates the intermediate termPositionsCache_.
    // Each token does ONE hash lookup in fieldPostings_ instead of TWO (cache + postings).
    // Freq is tracked in-place via pendingFreqIndex.
    auto& postingMap = fieldPostings_[fieldId];
    FieldStats& stats = fieldStats_[fieldId];

    for (int pos = 0; pos < static_cast<int>(tokens.size()); pos++) {
        const std::string& term = tokens[pos];
        auto [it, inserted] = postingMap.try_emplace(term);
        PostingData& data = it->second;

        if (inserted) {
            // Brand new term — initialize posting list
            data.postings.reserve(100);
            data.lastDocID = docID;
            data.postings.push_back(docID);
            data.pendingFreqIndex = static_cast<int>(data.postings.size());
            data.postings.push_back(1);  // freq starts at 1
            if (storePositions) data.postings.push_back(pos);

            bytesUsed_ += term.capacity() + 100 * sizeof(int) + 64;
            stats.sumDocFreq++;
            stats.sumTotalTermFreq++;
        } else if (data.lastDocID != docID) {
            // Existing term, new document
            size_t oldCap = data.postings.capacity();
            data.lastDocID = docID;
            data.postings.push_back(docID);
            data.pendingFreqIndex = static_cast<int>(data.postings.size());
            data.postings.push_back(1);
            if (storePositions) data.postings.push_back(pos);
            size_t newCap = data.postings.capacity();
            if (newCap > oldCap) bytesUsed_ += (newCap - oldCap) * sizeof(int);

            stats.sumDocFreq++;
            stats.sumTotalTermFreq++;
        } else {
            // Same term, same document — increment freq in-place, append position
            size_t oldCap = data.postings.capacity();
            data.postings[data.pendingFreqIndex]++;
            if (storePositions) data.postings.push_back(pos);
            size_t newCap = data.postings.capacity();
            if (newCap > oldCap) bytesUsed_ += (newCap - oldCap) * sizeof(int);

            stats.sumTotalTermFreq++;
        }
    }
}

// ==================== Query / Read Methods ====================

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& term) const {
    // Legacy: search all fields
    for (int i = 0; i < nextFieldId_; i++) {
        auto it = fieldPostings_[i].find(term);
        if (it != fieldPostings_[i].end()) {
            return it->second.postings;
        }
    }
    return {};
}

std::vector<std::string> FreqProxTermsWriter::getTerms() const {
    std::set<std::string> uniqueTerms;
    for (int i = 0; i < nextFieldId_; i++) {
        for (const auto& [term, _] : fieldPostings_[i]) {
            uniqueTerms.insert(term);
        }
    }
    return std::vector<std::string>(uniqueTerms.begin(), uniqueTerms.end());
}

std::vector<int> FreqProxTermsWriter::getPostingList(const std::string& field,
                                                     const std::string& term) const {
    auto fieldIdIt = fieldNameToId_.find(field);
    if (fieldIdIt == fieldNameToId_.end()) {
        return {};
    }

    auto it = fieldPostings_[fieldIdIt->second].find(term);
    if (it == fieldPostings_[fieldIdIt->second].end()) {
        return {};
    }
    return it->second.postings;
}

std::vector<std::string> FreqProxTermsWriter::getTermsForField(const std::string& field) const {
    auto fieldIdIt = fieldNameToId_.find(field);
    if (fieldIdIt == fieldNameToId_.end()) {
        return {};
    }

    // Collect terms from per-field posting map and sort (deferred from indexing time)
    const auto& postingMap = fieldPostings_[fieldIdIt->second];
    std::vector<std::string> terms;
    terms.reserve(postingMap.size());
    for (const auto& [term, _] : postingMap) {
        terms.push_back(term);
    }
    std::sort(terms.begin(), terms.end());
    return terms;
}

FreqProxTermsWriter::FieldStats
FreqProxTermsWriter::getFieldStats(const std::string& fieldName) const {
    auto it = fieldNameToId_.find(fieldName);
    if (it != fieldNameToId_.end() && it->second < static_cast<int>(fieldStats_.size())) {
        return fieldStats_[it->second];
    }
    return FieldStats{};
}

int64_t FreqProxTermsWriter::bytesUsed() const {
    return bytesUsed_ + termBytePool_.bytesUsed();
}

void FreqProxTermsWriter::reset() {
    termBytePool_.reset();
    for (int i = 0; i < nextFieldId_; i++) {
        fieldPostings_[i].clear();
        fieldLengths_[i].lengths.clear();
        fieldStats_[i] = FieldStats{};
    }
    fieldNameToId_.clear();
    idToFieldName_.clear();
    nextFieldId_ = 0;
    bytesUsed_ = 0;
}

void FreqProxTermsWriter::clear() {
    termBytePool_.clear();
    fieldPostings_.clear();
    fieldLengths_.clear();
    fieldStats_.clear();
    fieldNameToId_.clear();
    idToFieldName_.clear();
    nextFieldId_ = 0;
    bytesUsed_ = 0;
}

}  // namespace index
}  // namespace diagon
