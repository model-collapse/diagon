// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/Terms.h"
#include "diagon/store/IndexInput.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Lucene104FieldsProducer - Read term dictionaries and postings using Lucene104 format
 *
 * Phase 4.2 Implementation (Read Path):
 * - Reads .tim (term dictionary) and .tip (FST index) files
 * - Uses BlockTreeTermsReader for term lookup
 * - Integrates with Lucene104PostingsReader for postings iteration
 *
 * Files Read:
 * - .doc: Document IDs and frequencies (StreamVByte encoded)
 * - .tim: Term dictionary (block tree structure)
 * - .tip: Term dictionary index (FST)
 *
 * Thread Safety: Thread-safe for concurrent reads after construction.
 */
class Lucene104FieldsProducer : public FieldsProducer {
public:
    /**
     * Constructor - opens index files and initializes readers
     *
     * @param state Segment read state (directory, segment name, field infos)
     */
    explicit Lucene104FieldsProducer(::diagon::index::SegmentReadState& state);

    /**
     * Destructor - closes all index files
     */
    ~Lucene104FieldsProducer() override;

    /**
     * Get terms for a field
     *
     * @param field Field name
     * @return Terms instance, or nullptr if field doesn't exist
     */
    std::unique_ptr<::diagon::index::Terms> terms(const std::string& field) override;

    /**
     * Check integrity (FieldsProducer interface)
     */
    void checkIntegrity() override {
        // Phase 4.2: No checksum validation yet
    }

    /**
     * Close all input files
     */
    void close() override;

    /**
     * Get postings reader (for SegmentTermsEnum)
     */
    Lucene104PostingsReader* getPostingsReader() { return postingsReader_.get(); }

private:
    ::diagon::index::SegmentReadState& state_;

    // Input files
    std::unique_ptr<store::IndexInput> timIn_;  // .tim file (term dictionary)
    std::unique_ptr<store::IndexInput> tipIn_;  // .tip file (FST index)

    // Readers
    std::unique_ptr<Lucene104PostingsReader> postingsReader_;
    std::unordered_map<std::string, std::unique_ptr<blocktree::BlockTreeTermsReader>> termsReaders_;

    bool closed_{false};

    /**
     * Get or create terms reader for a field
     */
    blocktree::BlockTreeTermsReader* getTermsReader(const std::string& field);
};

/**
 * Lucene104Terms - Terms implementation for Lucene104FieldsProducer
 *
 * Wrapper around BlockTreeTermsReader that also provides postings via Lucene104PostingsReader.
 */
class Lucene104Terms : public ::diagon::index::Terms {
public:
    /**
     * Constructor
     *
     * @param termsReader BlockTree terms reader
     * @param postingsReader Postings reader
     * @param fieldInfo Field metadata
     */
    Lucene104Terms(blocktree::BlockTreeTermsReader* termsReader,
                   Lucene104PostingsReader* postingsReader,
                   const ::diagon::index::FieldInfo* fieldInfo);

    /**
     * Get iterator over terms
     */
    std::unique_ptr<::diagon::index::TermsEnum> iterator() const override;

    /**
     * Get number of terms in field
     */
    int64_t size() const override;

private:
    blocktree::BlockTreeTermsReader* termsReader_;  // Not owned
    Lucene104PostingsReader* postingsReader_;       // Not owned
    const ::diagon::index::FieldInfo* fieldInfo_;   // Not owned
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
