// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/BytesRef.h"
#include "diagon/search/DocIdSetIterator.h"

#include <cstdint>
#include <memory>

namespace diagon {
namespace index {

/**
 * Base interface for iterating over per-document values.
 *
 * Based on: org.apache.lucene.index.DocValuesIterator
 *
 * DocValues enable column-oriented access to per-document fields for:
 * - Sorting and faceting
 * - Aggregations
 * - Fast lookups without decompressing entire fields
 */
class DocValuesIterator : public search::DocIdSetIterator {
public:
    virtual ~DocValuesIterator() = default;

    /**
     * Advance to exactly the specified document.
     * Returns true if the document has a value, false otherwise.
     *
     * This is more efficient than advance() when you know the exact target.
     *
     * @param target Document ID to position to
     * @return true if doc has a value, false if it doesn't
     */
    virtual bool advanceExact(int target) = 0;
};

/**
 * Iterates over numeric doc values (64-bit signed integers).
 *
 * Based on: org.apache.lucene.index.NumericDocValues
 *
 * Use cases:
 * - Timestamps, scores, ratings
 * - Numeric facets
 * - Sorting by numeric field
 */
class NumericDocValues : public DocValuesIterator {
public:
    virtual ~NumericDocValues() = default;

    /**
     * Get the numeric value for the current document.
     * Only valid after next() or advanceExact() returns true.
     *
     * @return 64-bit signed integer value
     */
    virtual int64_t longValue() const = 0;
};

/**
 * Iterates over binary doc values (variable-length byte arrays).
 *
 * Based on: org.apache.lucene.index.BinaryDocValues
 *
 * Use cases:
 * - Short strings (< 32KB)
 * - Checksums, binary data
 * - Raw byte storage
 */
class BinaryDocValues : public DocValuesIterator {
public:
    virtual ~BinaryDocValues() = default;

    /**
     * Get the binary value for the current document.
     * Only valid after next() or advanceExact() returns true.
     *
     * The returned BytesRef is only valid until the next call to
     * next(), advance(), or advanceExact().
     *
     * @return Binary value as BytesRef
     */
    virtual util::BytesRef binaryValue() const = 0;
};

/**
 * Iterates over sorted doc values (single deduplicated value per document).
 *
 * Based on: org.apache.lucene.index.SortedDocValues
 *
 * Use cases:
 * - String facets
 * - Categories
 * - Sorting by string field
 *
 * NOTE: Not implemented in Phase 2 MVP - stub interface only
 */
class SortedDocValues : public DocValuesIterator {
public:
    virtual ~SortedDocValues() = default;

    /**
     * Get the ordinal for the current document.
     * Returns -1 if the document has no value.
     *
     * @return Ordinal (0 to getValueCount()-1) or -1
     */
    virtual int ordValue() const = 0;

    /**
     * Lookup the term for a given ordinal.
     *
     * @param ord Ordinal to lookup
     * @return Term as BytesRef
     */
    virtual util::BytesRef lookupOrd(int ord) const = 0;

    /**
     * Get the number of unique values in the dictionary.
     *
     * @return Number of unique values
     */
    virtual int getValueCount() const = 0;
};

/**
 * Iterates over sorted-set doc values (multiple deduplicated values per document).
 *
 * Based on: org.apache.lucene.index.SortedSetDocValues
 *
 * Use cases:
 * - Tags, multi-select facets
 * - Multiple categories per document
 *
 * NOTE: Not implemented in Phase 2 MVP - stub interface only
 */
class SortedSetDocValues : public DocValuesIterator {
public:
    virtual ~SortedSetDocValues() = default;

    /**
     * Get the next ordinal for the current document.
     * Returns NO_MORE_ORDS when no more ordinals remain.
     */
    virtual int64_t nextOrd() = 0;

    static constexpr int64_t NO_MORE_ORDS = -1;

    /**
     * Lookup the term for a given ordinal.
     */
    virtual util::BytesRef lookupOrd(int64_t ord) const = 0;

    /**
     * Get the number of unique values in the dictionary.
     */
    virtual int64_t getValueCount() const = 0;
};

/**
 * Iterates over sorted-numeric doc values (multiple numeric values per document).
 *
 * Based on: org.apache.lucene.index.SortedNumericDocValues
 *
 * Use cases:
 * - Multi-valued numeric attributes
 * - Percentile aggregations
 *
 * NOTE: Not implemented in Phase 2 MVP - stub interface only
 */
class SortedNumericDocValues : public DocValuesIterator {
public:
    virtual ~SortedNumericDocValues() = default;

    /**
     * Get the next value for the current document.
     * Must be called exactly docValueCount() times after advance().
     *
     * @return Next numeric value in ascending order
     */
    virtual int64_t nextValue() = 0;

    /**
     * Get the number of values for the current document.
     *
     * @return Number of values (>= 1)
     */
    virtual int docValueCount() const = 0;
};

}  // namespace index
}  // namespace diagon
