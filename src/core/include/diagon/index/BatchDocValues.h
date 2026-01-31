// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/DocValues.h"
#include "diagon/columns/ColumnVector.h"
#include <memory>

namespace diagon {
namespace index {

/**
 * BatchNumericDocValues - Batch-at-a-time numeric doc values
 *
 * Extends NumericDocValues with batch lookup capability to eliminate
 * virtual call overhead in hot loops.
 *
 * ## Motivation
 *
 * Phase 4 analysis showed that norm lookups during BM25 scoring require
 * 2 virtual calls per document (advanceExact + longValue), contributing
 * to 32.79% overhead when batching 8 documents.
 *
 * Batch lookup pattern (inspired by QBlock's ColumnVector access):
 * - Single method call for N documents
 * - Direct array access inside (no virtual calls in loop)
 * - Compiler can vectorize and prefetch
 * - Cache-friendly sequential or random access
 *
 * ## Performance Model
 *
 * One-at-a-time: 8 docs × 2 virtual calls = 16 × 15 cycles = 240 cycles
 * Batch: 1 virtual call + 8 array lookups = 15 + 24 cycles = 39 cycles
 * Speedup: 6.15×
 *
 * ## Example
 *
 * ```cpp
 * BatchNumericDocValues* batchNorms = ...;
 * int docs[8] = {10, 25, 37, ...};
 * long norms[8];
 *
 * // Single call, no virtual calls in loop
 * batchNorms->getBatch(docs, norms, 8);
 *
 * // Now process with SIMD
 * __m256 lengths = decode_norms_simd(norms);
 * ```
 */
class BatchNumericDocValues : public NumericDocValues {
public:
    virtual ~BatchNumericDocValues() = default;

    /**
     * Get values for batch of documents
     *
     * Fills values array with document values. Order matches docs array.
     * For missing documents, fills with default value (typically 0 or 1).
     *
     * @param docs Document IDs [count]
     * @param values Output values [count]
     * @param count Number of documents
     *
     * Implementation notes:
     * - Should NOT contain virtual calls in loop
     * - Should use direct array/ColumnVector access
     * - Should be inlinable for maximum performance
     * - Should prefetch for random access patterns
     */
    virtual void getBatch(const int* docs, long* values, int count) = 0;

    // ==================== Legacy One-at-a-Time API ====================

    /**
     * Implement one-at-a-time using batch underneath
     */
    bool advanceExact(int doc) override {
        long value;
        getBatch(&doc, &value, 1);
        cached_value_ = value;
        return true;
    }

    long longValue() const override {
        return cached_value_;
    }

protected:
    long cached_value_ = 0;
};

/**
 * ColumnVectorNumericDocValues - ColumnVector-based batch doc values
 *
 * Implementation using ColumnVector<int64_t> for zero-copy access
 * and optimal batch performance.
 *
 * ## Design
 *
 * Based on QBlock's ColumnVector storage pattern:
 * - Data stored in contiguous PODArray
 * - Direct pointer access via getRawData()
 * - No virtual calls in hot loop
 * - Optimal for mmap and cache locality
 *
 * ## Example
 *
 * ```cpp
 * auto column = ColumnVector<int64_t>::create(num_docs);
 * // ... fill column with norms ...
 *
 * auto docValues = std::make_unique<ColumnVectorNumericDocValues>(column);
 *
 * int docs[8] = {...};
 * long norms[8];
 * docValues->getBatch(docs, norms, 8);  // Fast: direct array access
 * ```
 */
class ColumnVectorNumericDocValues : public BatchNumericDocValues {
public:
    /**
     * Construct from ColumnVector
     *
     * @param column Column containing int64 values
     */
    explicit ColumnVectorNumericDocValues(
        std::shared_ptr<columns::ColumnVector<int64_t>> column)
        : column_(column) {
        // Cache raw pointer for fast access
        data_ = reinterpret_cast<const int64_t*>(column_->getRawData());
        size_ = column_->size();
    }

    /**
     * Batch lookup - NO virtual calls in loop
     *
     * Pure array access with compiler optimization opportunities.
     */
    void getBatch(const int* docs, long* values, int count) override {
        // Direct array access - compiler can vectorize/prefetch
        for (int i = 0; i < count; i++) {
            int doc = docs[i];
            // Bounds check (will be optimized out if docs are known valid)
            values[i] = (doc >= 0 && doc < static_cast<int>(size_))
                ? data_[doc]
                : 1L;  // Default norm
        }
    }

    /**
     * Optimized single-document access
     */
    bool advanceExact(int doc) override {
        if (doc >= 0 && doc < static_cast<int>(size_)) {
            cached_value_ = data_[doc];
            return true;
        }
        cached_value_ = 1L;
        return false;
    }

    long longValue() const override {
        return cached_value_;
    }

private:
    std::shared_ptr<columns::ColumnVector<int64_t>> column_;
    const int64_t* data_;  // Cached pointer for fast access
    size_t size_;
};

}  // namespace index
}  // namespace diagon
