// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/Terms.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {

// Forward declaration
namespace index {
struct SegmentReadState;
}  // namespace index

namespace codecs {
namespace lucene104 {

/**
 * FieldsProducer for Lucene104 format.
 *
 * Reads .tim and .tip files using BlockTreeTermsReader.
 */
class Lucene104FieldsProducer : public FieldsProducer {
public:
    /**
     * Constructor
     *
     * @param state Segment read state
     */
    explicit Lucene104FieldsProducer(index::SegmentReadState& state);

    /**
     * Destructor
     */
    ~Lucene104FieldsProducer() override;

    // ==================== FieldsProducer Interface ====================

    /**
     * Get Terms for a field
     */
    std::unique_ptr<index::Terms> terms(const std::string& field) override;

    /**
     * Check integrity (checksums)
     */
    void checkIntegrity() override;

    /**
     * Close resources
     */
    void close() override;

private:
    /**
     * Field-level metadata read from .tmd file
     */
    struct FieldMetadata {
        int64_t numTerms;
        int64_t sumTotalTermFreq;
        int64_t sumDocFreq;
        int docCount;
    };

    /**
     * Holds a field reader and its cloned inputs.
     * Each field needs independent IndexInput clones to avoid file pointer conflicts.
     */
    struct FieldReaderHolder {
        std::shared_ptr<blocktree::BlockTreeTermsReader> reader;
        std::unique_ptr<store::IndexInput> timInputClone;
        std::unique_ptr<store::IndexInput> tipInputClone;
    };

    std::string segmentName_;
    const index::FieldInfos& fieldInfos_;

    // Field metadata map (fieldName -> stats)
    std::map<std::string, FieldMetadata> fieldMetadata_;

    // Index inputs for .tim and .tip files
    std::unique_ptr<store::IndexInput> timInput_;
    std::unique_ptr<store::IndexInput> tipInput_;

    // Postings reader for retrieving doc IDs and frequencies
    std::unique_ptr<class Lucene104PostingsReader> postingsReader_;

    // Per-field term readers (lazy loaded, each with independent input clones)
    mutable std::unordered_map<std::string, FieldReaderHolder> fieldReaders_;
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
