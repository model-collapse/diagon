// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/NormsFormat.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexInput.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Lucene104NormsReader - Read norms using Lucene 10.4 format
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90NormsProducer
 *
 * Reads norms data written by Lucene104NormsWriter.
 * Provides NumericDocValues interface for accessing norms during scoring.
 */
class Lucene104NormsReader : public NormsProducer {
public:
    /**
     * Constructor
     *
     * @param state Segment read state
     */
    explicit Lucene104NormsReader(index::SegmentReadState& state);

    /**
     * Destructor
     */
    ~Lucene104NormsReader() override;

    // ==================== NormsProducer Implementation ====================

    /**
     * Get norms for a field
     *
     * @param field Field metadata
     * @return Iterator over norms (doc → norm value)
     */
    std::unique_ptr<index::NumericDocValues> getNorms(const index::FieldInfo& field) override;

    /**
     * Check integrity
     */
    void checkIntegrity() override;

    /**
     * Close
     */
    void close() override;

private:
    /**
     * Field metadata (from .nvm file)
     */
    struct FieldMetadata {
        int fieldNumber;
        int64_t offset;  // Offset in .nvd file
        int count;       // Number of documents
    };

    /**
     * Read metadata file (.nvm)
     */
    void readMetadata();

    /**
     * Load norms for a field from disk
     *
     * @param field Field metadata
     * @return Norms array (doc → byte)
     */
    std::vector<int8_t> loadNorms(const index::FieldInfo& field);

    /**
     * Decode norm byte to similarity value
     *
     * @param norm Encoded norm byte
     * @return Decoded value (used in scoring)
     */
    static float decodeNormValue(int8_t norm);

    /**
     * NormsValues - In-memory norms iterator
     */
    class NormsValues : public index::NumericDocValues {
    public:
        explicit NormsValues(std::vector<int8_t> norms)
            : norms_(std::move(norms))
            , docID_(-1) {}

        bool advanceExact(int target) override {
            docID_ = target;
            return target >= 0 && target < static_cast<int>(norms_.size());
        }

        int64_t longValue() const override {
            if (docID_ < 0 || docID_ >= static_cast<int>(norms_.size())) {
                return 0;
            }
            // Return raw byte value (decoding happens in scorer)
            return static_cast<int64_t>(norms_[docID_]);
        }

        int docID() const override { return docID_; }

        int nextDoc() override {
            if (docID_ + 1 < static_cast<int>(norms_.size())) {
                docID_++;
                return docID_;
            }
            docID_ = NO_MORE_DOCS;
            return NO_MORE_DOCS;
        }

        int advance(int target) override {
            if (target >= static_cast<int>(norms_.size())) {
                docID_ = NO_MORE_DOCS;
                return NO_MORE_DOCS;
            }
            docID_ = target;
            return docID_;
        }

        int64_t cost() const override { return norms_.size(); }

        /** Direct access to norm array (eliminates virtual dispatch for batch norms lookup) */
        const int8_t* normsData(int* outSize) const override {
            if (outSize)
                *outSize = static_cast<int>(norms_.size());
            return norms_.data();
        }

    private:
        std::vector<int8_t> norms_;
        int docID_;
    };

    [[maybe_unused]] index::SegmentReadState& state_;
    std::unique_ptr<store::IndexInput> data_;  // .nvd file
    std::unique_ptr<store::IndexInput> meta_;  // .nvm file
    bool closed_{false};

    // Field metadata map (fieldNumber → metadata)
    std::unordered_map<int, FieldMetadata> fieldMetadata_;

    // Cache of loaded norms (fieldNumber → norms array)
    std::unordered_map<int, std::vector<int8_t>> normsCache_;
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
