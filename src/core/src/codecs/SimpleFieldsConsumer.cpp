// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/SimpleFieldsConsumer.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <sstream>

using diagon::AlreadyClosedException;

namespace diagon {
namespace codecs {

// Magic number: "POST" in ASCII
static const int32_t MAGIC = 0x504F5354;
static const int32_t VERSION = 1;

SimpleFieldsConsumer::SimpleFieldsConsumer(const index::SegmentWriteState& state)
    : state_(state) {

    // Create output file
    std::string fileName = getPostingsFileName();
    output_ = state_.directory->createOutput(fileName, state_.context);
    files_.push_back(fileName);

    // Don't write header yet - will write in writeField() when we know term count
}

SimpleFieldsConsumer::~SimpleFieldsConsumer() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

std::string SimpleFieldsConsumer::getPostingsFileName() const {
    std::ostringstream oss;
    oss << state_.segmentName;
    if (!state_.segmentSuffix.empty()) {
        oss << "_" << state_.segmentSuffix;
    }
    oss << ".post";
    return oss.str();
}

void SimpleFieldsConsumer::writeHeader() {
    // Write magic + version
    output_->writeInt(MAGIC);
    output_->writeInt(VERSION);

    // Write placeholder for term count (will update at close)
    output_->writeInt(0);
}

void SimpleFieldsConsumer::writeField(
    const std::string& fieldName,
    const std::unordered_map<std::string, std::vector<int>>& terms) {

    ensureOpen();

    // Sort terms for consistent ordering
    std::vector<std::string> sortedTerms;
    sortedTerms.reserve(terms.size());
    for (const auto& pair : terms) {
        sortedTerms.push_back(pair.first);
    }
    std::sort(sortedTerms.begin(), sortedTerms.end());

    // Write header with actual term count
    output_->writeInt(MAGIC);
    output_->writeInt(VERSION);
    output_->writeInt(static_cast<int32_t>(sortedTerms.size()));

    // Write each term's posting list
    for (const auto& term : sortedTerms) {
        const auto& postings = terms.at(term);

        // Write term as string
        output_->writeString(term);

        // Calculate number of postings (each posting is 2 ints: docID + freq)
        int32_t numPostings = static_cast<int32_t>(postings.size() / 2);
        output_->writeInt(numPostings);

        // Write postings: [docID, freq] pairs
        for (size_t i = 0; i < postings.size(); i += 2) {
            output_->writeInt(postings[i]);      // docID
            output_->writeInt(postings[i + 1]);  // freq
        }
    }
}

void SimpleFieldsConsumer::close() {
    if (closed_) {
        return;
    }

    ensureOpen();

    // Close output
    if (output_) {
        output_->close();
        output_.reset();
    }

    closed_ = true;
}

void SimpleFieldsConsumer::ensureOpen() const {
    if (closed_) {
        throw AlreadyClosedException("SimpleFieldsConsumer is closed");
    }
}

}  // namespace codecs
}  // namespace diagon
