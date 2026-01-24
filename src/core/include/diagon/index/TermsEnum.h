// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/BytesRef.h"
#include "diagon/index/PostingsEnum.h"

#include <memory>

namespace diagon {
namespace index {

/**
 * Iterator over terms in a field.
 *
 * Based on: org.apache.lucene.index.TermsEnum
 *
 * Terms are returned in sorted (UTF-8 byte) order.
 *
 * Usage:
 * ```cpp
 * TermsEnum* terms = ...;
 * while (terms->next()) {
 *     BytesRef term = terms->term();
 *     int docFreq = terms->docFreq();
 *     // Process term...
 * }
 * ```
 */
class TermsEnum {
public:
    /**
     * Seek status for seekCeil operation.
     */
    enum class SeekStatus {
        /** Term was found */
        FOUND,
        /** Term not found, positioned at next term */
        NOT_FOUND,
        /** Term not found, no more terms */
        END
    };

    virtual ~TermsEnum() = default;

    /**
     * Increments to next term.
     *
     * @return true if a term exists, false if no more terms
     */
    virtual bool next() = 0;

    /**
     * Seeks to exact term.
     *
     * @param text Term to seek to
     * @return true if term exists, false otherwise
     */
    virtual bool seekExact(const util::BytesRef& text) = 0;

    /**
     * Seeks to ceiling (term >= target).
     *
     * @param text Target term
     * @return SeekStatus indicating result
     */
    virtual SeekStatus seekCeil(const util::BytesRef& text) = 0;

    /**
     * Returns current term.
     *
     * @return Current term (valid until next() or seek)
     */
    virtual util::BytesRef term() const = 0;

    /**
     * Returns document frequency (number of docs containing this term).
     *
     * @return Document frequency
     */
    virtual int docFreq() const = 0;

    /**
     * Returns total term frequency (total occurrences across all docs).
     *
     * @return Total term frequency
     */
    virtual int64_t totalTermFreq() const = 0;

    /**
     * Returns postings for current term.
     *
     * @return PostingsEnum for iterating doc IDs
     */
    virtual std::unique_ptr<PostingsEnum> postings() = 0;
};

}  // namespace index
}  // namespace diagon
