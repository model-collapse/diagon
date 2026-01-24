// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexOutput.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * SimpleFieldsConsumer - Basic posting list writer for Phase 3
 *
 * Writes posting lists in a simple, uncompressed format for testing the
 * flush pipeline. This is NOT production-quality - just enough to validate
 * the end-to-end flow from DWPT → Codec → Directory → SegmentInfo.
 *
 * File Format (.post file):
 * -------------------------
 * [Header]
 *   Magic: 0x504F5354 ("POST")
 *   Version: 1 (int32)
 *   NumTerms: N (int32)
 *
 * [Terms]
 *   For each term:
 *     TermLength: L (int32)
 *     TermBytes: [L bytes]
 *     NumPostings: P (int32)
 *     Postings: [docID (int32), freq (int32)] * P
 *
 * Phase 4 will add:
 * - FST term dictionary
 * - Delta encoding + compression
 * - Skip lists
 * - Block-based storage
 *
 * Thread Safety: NOT thread-safe (single-threaded flush)
 */
class SimpleFieldsConsumer : public FieldsConsumer {
public:
    /**
     * Constructor
     *
     * @param state Segment write state (directory, segment name, etc.)
     */
    explicit SimpleFieldsConsumer(const index::SegmentWriteState& state);

    /**
     * Destructor - closes output if not already closed
     */
    ~SimpleFieldsConsumer() override;

    /**
     * Write a field's posting lists
     *
     * @param fieldName Field name
     * @param terms Map of term → posting list
     *              Posting list format: [docID, freq, docID, freq, ...]
     */
    void writeField(
        const std::string& fieldName,
        const std::unordered_map<std::string, std::vector<int>>& terms);

    /**
     * Close and flush all data
     */
    void close() override;

    /**
     * Get list of files created
     */
    const std::vector<std::string>& getFiles() const {
        return files_;
    }

private:
    // Segment write state
    index::SegmentWriteState state_;

    // Output stream
    std::unique_ptr<store::IndexOutput> output_;

    // Files created
    std::vector<std::string> files_;

    // Closed flag
    bool closed_{false};

    /**
     * Get output filename for posting lists
     */
    std::string getPostingsFileName() const;

    /**
     * Write magic header
     */
    void writeHeader();

    /**
     * Validate state before writing
     */
    void ensureOpen() const;
};

}  // namespace codecs
}  // namespace diagon
