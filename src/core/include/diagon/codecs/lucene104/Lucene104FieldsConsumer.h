// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/index/Fields.h"
#include "diagon/index/SegmentWriteState.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {

// Forward declarations
namespace store {
class IndexOutput;
}

namespace codecs {
namespace blocktree {
class BlockTreeTermsWriter;
}

namespace lucene104 {

// Forward declarations
class Lucene104PostingsWriter;

/**
 * Lucene104FieldsConsumer - Write posting lists using Lucene104 format
 *
 * Proper streaming API implementation using Fields/Terms/TermsEnum.
 *
 * Phase 4.1 Implementation (BlockTreeTermsWriter Integration):
 * - Uses proper streaming "pull" API: write(Fields fields)
 * - Iterates over fields, terms, and postings
 * - Uses StreamVByte encoding for postings
 * - Integrates BlockTreeTermsWriter for term dictionary
 *
 * Files Created:
 * - .doc: Document IDs and frequencies (StreamVByte encoded)
 * - .tim: Term dictionary (block tree structure)
 * - .tip: Term dictionary index (FST)
 */
class Lucene104FieldsConsumer : public FieldsConsumer {
public:
    /**
     * Constructor
     *
     * @param state Segment write state
     */
    explicit Lucene104FieldsConsumer(::diagon::index::SegmentWriteState& state);

    /**
     * Destructor
     */
    ~Lucene104FieldsConsumer() override;

    /**
     * Write all fields, terms and postings using streaming API
     *
     * Implements proper Lucene "pull" API:
     * - Iterate over all fields
     * - For each field, iterate over all terms
     * - For each term, iterate over all postings
     * - Write to disk format
     *
     * @param fields Fields to write (provides iterator)
     * @param norms Norms producer (optional)
     */
    void write(::diagon::index::Fields& fields, ::diagon::codecs::NormsProducer* norms) override;

    /**
     * Close and flush
     */
    void close() override;

    /**
     * Get files created
     */
    const std::vector<std::string>& getFiles() const { return files_; }

private:
    /**
     * Field-level metadata stored per field
     */
    struct FieldMetadata {
        int64_t numTerms;
        int64_t sumTotalTermFreq;
        int64_t sumDocFreq;
        int docCount;
    };

    ::diagon::index::SegmentWriteState& state_;
    std::unique_ptr<Lucene104PostingsWriter> postingsWriter_;

    // Term dictionary outputs
    std::unique_ptr<store::IndexOutput> timOut_;  // .tim file (term blocks)
    std::unique_ptr<store::IndexOutput> tipOut_;  // .tip file (FST index)

    std::vector<std::string> files_;
    bool closed_{false};

    // Field metadata map (fieldName -> stats)
    std::map<std::string, FieldMetadata> fieldMetadata_;

    /**
     * Write a single field
     *
     * @param fieldName Field name
     * @param terms Terms for this field
     * @param norms Norms producer (optional, for Block-Max WAND impacts)
     */
    void writeField(const std::string& fieldName, ::diagon::index::Terms& terms,
                    ::diagon::codecs::NormsProducer* norms);
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
