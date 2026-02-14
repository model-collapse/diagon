// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/NormsFormat.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/FieldInfo.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace index {

/**
 * In-memory NormsProducer for norms computed during indexing
 *
 * Holds field length norms for documents before they're flushed to disk.
 */
class InMemoryNormsProducer : public codecs::NormsProducer {
public:
    InMemoryNormsProducer() = default;
    ~InMemoryNormsProducer() override = default;

    /**
     * Set norm value for a document in a field
     *
     * @param field Field name
     * @param docID Document ID
     * @param norm Encoded norm value (0-127)
     */
    void setNorm(const std::string& field, int docID, int8_t norm);

    /**
     * Compute and set norm from field length
     *
     * @param field Field name
     * @param docID Document ID
     * @param fieldLength Number of terms in field
     */
    void setNormFromLength(const std::string& field, int docID, int fieldLength);

    // NormsProducer interface
    std::unique_ptr<NumericDocValues> getNorms(const FieldInfo& field) override;
    void checkIntegrity() override {}
    void close() override {}

private:
    // Field name -> (docID -> norm value)
    std::unordered_map<std::string, std::vector<int8_t>> norms_;

    /**
     * Encode field length into norm byte (Lucene-compatible encoding)
     *
     * Encodes 1/sqrt(fieldLength) into a byte using:
     * - norm = (byte)(256 * lengthNorm)
     * - Values range from 0 (longest docs) to 255 (shortest docs)
     */
    static int8_t encodeNorm(int fieldLength);
};

/**
 * In-memory NumericDocValues for accessing norms
 */
class InMemoryNormValues : public NumericDocValues {
public:
    explicit InMemoryNormValues(std::vector<int8_t> norms)
        : norms_(std::move(norms))
        , currentDoc_(-1)
        , currentValue_(0) {}

    bool advanceExact(int target) override {
        if (target < 0 || target >= static_cast<int>(norms_.size())) {
            currentDoc_ = -1;
            return false;
        }
        currentDoc_ = target;
        currentValue_ = norms_[target];
        return true;
    }

    int64_t longValue() const override { return static_cast<int64_t>(currentValue_); }

    int docID() const override { return currentDoc_; }

    int nextDoc() override {
        if (currentDoc_ + 1 >= static_cast<int>(norms_.size())) {
            currentDoc_ = NO_MORE_DOCS;
        } else {
            currentDoc_++;
            currentValue_ = norms_[currentDoc_];
        }
        return currentDoc_;
    }

    int advance(int target) override {
        if (target >= static_cast<int>(norms_.size())) {
            currentDoc_ = NO_MORE_DOCS;
            return NO_MORE_DOCS;
        }
        currentDoc_ = target;
        currentValue_ = norms_[currentDoc_];
        return currentDoc_;
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
    int currentDoc_;
    int8_t currentValue_;
};

}  // namespace index
}  // namespace diagon
