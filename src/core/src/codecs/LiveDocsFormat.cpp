// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/LiveDocsFormat.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

void LiveDocsFormat::writeLiveDocs(store::Directory& directory, const std::string& segmentName,
                                   const util::BitSet& liveDocs, int delCount) {
    std::string fileName = segmentName + EXTENSION;

    // Create output file
    auto output = directory.createOutput(fileName, store::IOContext::DEFAULT);

    // Write header
    output->writeString(CODEC_NAME);
    output->writeVInt(VERSION);

    // Write num docs and del count
    output->writeVInt(static_cast<int>(liveDocs.length()));
    output->writeVInt(delCount);

    // Write bitset words
    const uint64_t* bits = liveDocs.getBits();
    size_t numWords = liveDocs.numWords();

    for (size_t i = 0; i < numWords; i++) {
        output->writeLong(static_cast<int64_t>(bits[i]));
    }

    // Close
    output->close();
}

std::unique_ptr<util::BitSet> LiveDocsFormat::readLiveDocs(store::Directory& directory,
                                                           const std::string& segmentName,
                                                           int maxDoc) {
    std::string fileName = segmentName + EXTENSION;

    // Check if file exists
    try {
        auto input = directory.openInput(fileName, store::IOContext::READ);

        // Read header
        std::string codec = input->readString();
        if (codec != CODEC_NAME) {
            throw std::runtime_error("Invalid codec: expected " + std::string(CODEC_NAME) +
                                     ", got " + codec);
        }

        int version = input->readVInt();
        if (version != VERSION) {
            throw std::runtime_error("Invalid version: expected " + std::to_string(VERSION) +
                                     ", got " + std::to_string(version));
        }

        // Read num docs and del count
        int numDocs = input->readVInt();
        if (numDocs != maxDoc) {
            throw std::runtime_error("NumDocs mismatch: expected " + std::to_string(maxDoc) +
                                     ", got " + std::to_string(numDocs));
        }

        int delCount = input->readVInt();
        (void)delCount;  // Not used currently, but validates file format

        // Read bitset words
        size_t numWords = util::BitSet::bits2words(maxDoc);
        std::vector<uint64_t> words(numWords);

        for (size_t i = 0; i < numWords; i++) {
            words[i] = static_cast<uint64_t>(input->readLong());
        }

        // Create BitSet from words
        return std::make_unique<util::BitSet>(std::move(words), maxDoc);

    } catch (const std::exception&) {
        // File doesn't exist or error reading - no deletions
        return nullptr;
    }
}

bool LiveDocsFormat::liveDocsExist(store::Directory& directory, const std::string& segmentName) {
    std::string fileName = segmentName + EXTENSION;

    try {
        auto input = directory.openInput(fileName, store::IOContext::READ);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace codecs
}  // namespace diagon
