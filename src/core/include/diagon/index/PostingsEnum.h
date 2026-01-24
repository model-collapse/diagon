// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/DocIdSetIterator.h"

namespace diagon {
namespace index {

/**
 * Iterates over postings (doc IDs and term frequencies).
 *
 * Extends DocIdSetIterator with term-specific data access.
 *
 * Based on: org.apache.lucene.index.PostingsEnum
 *
 * Usage:
 * ```cpp
 * PostingsEnum* postings = ...;
 * while (postings->nextDoc() != DocIdSetIterator::NO_MORE_DOCS) {
 *     int docID = postings->docID();
 *     int freq = postings->freq();
 *     // process doc
 * }
 * ```
 */
class PostingsEnum : public search::DocIdSetIterator {
public:
    /**
     * Term frequency in current document.
     * Only valid after nextDoc()/advance() returns a valid doc ID.
     *
     * @return Term frequency (>= 1), or 1 if frequencies not indexed
     */
    virtual int freq() const = 0;

    /**
     * Next position (for DOCS_AND_FREQS_AND_POSITIONS).
     * Call freq() times to iterate all positions.
     *
     * Phase 2 MVP: Not implemented yet
     * @return Position or -1
     */
    virtual int nextPosition() {
        return -1;  // Not supported in Phase 2 MVP
    }

    /**
     * Start offset of current position (for offsets).
     *
     * Phase 2 MVP: Not implemented yet
     * @return Start offset or -1
     */
    virtual int startOffset() const {
        return -1;  // Not supported in Phase 2 MVP
    }

    /**
     * End offset of current position (for offsets).
     *
     * Phase 2 MVP: Not implemented yet
     * @return End offset or -1
     */
    virtual int endOffset() const {
        return -1;  // Not supported in Phase 2 MVP
    }
};

}  // namespace index
}  // namespace diagon
