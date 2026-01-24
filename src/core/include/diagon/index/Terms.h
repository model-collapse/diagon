// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/TermsEnum.h"

#include <memory>

namespace diagon {
namespace index {

/**
 * Access to terms and posting lists for a field.
 *
 * Based on: org.apache.lucene.index.Terms
 *
 * Represents the term dictionary and posting lists for a single field.
 * Terms are sorted in UTF-8 byte order.
 *
 * Usage:
 * ```cpp
 * Terms* terms = reader->terms("field");
 * TermsEnum* termsEnum = terms->iterator();
 * while (termsEnum->next()) {
 *     BytesRef term = termsEnum->term();
 *     // Process term...
 * }
 * ```
 */
class Terms {
public:
    virtual ~Terms() = default;

    /**
     * Returns an iterator over all terms.
     *
     * @return TermsEnum iterator (never null)
     */
    virtual std::unique_ptr<TermsEnum> iterator() const = 0;

    /**
     * Returns number of terms in this field.
     *
     * @return Number of terms, or -1 if unknown
     */
    virtual int64_t size() const = 0;

    /**
     * Returns total number of documents that have at least one term for this field.
     *
     * @return Document count, or -1 if unknown
     */
    virtual int getDocCount() const {
        return -1;  // Optional, not all implementations track this
    }

    /**
     * Returns sum of TermsEnum.totalTermFreq for all terms.
     *
     * @return Sum of all term frequencies, or -1 if unknown
     */
    virtual int64_t getSumTotalTermFreq() const {
        return -1;  // Optional
    }

    /**
     * Returns sum of TermsEnum.docFreq for all terms.
     *
     * @return Sum of all document frequencies, or -1 if unknown
     */
    virtual int64_t getSumDocFreq() const {
        return -1;  // Optional
    }

    /**
     * Returns true if documents in this field store positions.
     *
     * @return true if positions are stored
     */
    virtual bool hasPositions() const {
        return false;  // Phase 4: No positions yet
    }

    /**
     * Returns true if documents in this field store offsets.
     *
     * @return true if offsets are stored
     */
    virtual bool hasOffsets() const {
        return false;  // Phase 4: No offsets yet
    }

    /**
     * Returns true if documents in this field store payloads.
     *
     * @return true if payloads are stored
     */
    virtual bool hasPayloads() const {
        return false;  // Phase 4: No payloads yet
    }
};

}  // namespace index
}  // namespace diagon
