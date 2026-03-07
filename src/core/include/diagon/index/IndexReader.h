// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/CacheHelper.h"
#include "diagon/index/LeafReaderContext.h"
#include "diagon/util/Bits.h"
#include "diagon/util/Exceptions.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace diagon {

// Forward declaration for codecs
namespace codecs {
class StoredFieldsReader;
}

namespace index {

// Forward declarations
class IndexReader;
class LeafReader;
class CompositeReader;
class FieldInfos;
class Terms;
class PostingsEnum;
class NumericDocValues;
class BinaryDocValues;
class SortedDocValues;
class SortedSetDocValues;
class SortedNumericDocValues;
class PointValues;

// ==================== Context Classes ====================

/**
 * Base context for IndexReader
 *
 * Based on: org.apache.lucene.index.IndexReaderContext
 */
class IndexReaderContext {
public:
    virtual ~IndexReaderContext() = default;

    virtual IndexReader* reader() const = 0;
    virtual std::vector<LeafReaderContext> leaves() const = 0;
    virtual bool isTopLevel() const = 0;
};

/**
 * Context wrapper for LeafReader (for polymorphic use)
 * Defined after LeafReader class
 */
class LeafReaderContextWrapper : public IndexReaderContext {
public:
    explicit LeafReaderContextWrapper(std::shared_ptr<LeafReader> reader);

    IndexReader* reader() const override;
    std::vector<LeafReaderContext> leaves() const override;
    bool isTopLevel() const override { return true; }

    const LeafReaderContext& leafContext() const { return ctx_; }

private:
    LeafReaderContext ctx_;
};

/**
 * Context for CompositeReader
 *
 * Based on: org.apache.lucene.index.CompositeReaderContext
 */
class CompositeReaderContext : public IndexReaderContext {
public:
    explicit CompositeReaderContext(CompositeReader* reader, std::vector<LeafReaderContext> leaves)
        : reader_(reader)
        , leaves_(std::move(leaves)) {}

    IndexReader* reader() const override;

    std::vector<LeafReaderContext> leaves() const override { return leaves_; }

    bool isTopLevel() const override { return true; }

private:
    CompositeReader* reader_;
    std::vector<LeafReaderContext> leaves_;
};

// ==================== IndexReader (Abstract Base) ====================

/**
 * IndexReader is an abstract base class providing read access to an index.
 *
 * Sealed hierarchy with two branches:
 * - LeafReader: atomic view of a single segment
 * - CompositeReader: composed view of multiple segments
 *
 * Thread-safe for concurrent reads.
 * Point-in-time snapshot semantics.
 *
 * Based on: org.apache.lucene.index.IndexReader
 */
class IndexReader : public std::enable_shared_from_this<IndexReader> {
public:
    virtual ~IndexReader() = default;

    // ==================== Context Access ====================

    /**
     * Returns leaf contexts for all segments
     * Each context contains: LeafReader, docBase, ord
     */
    virtual std::vector<LeafReaderContext> leaves() const = 0;

    /**
     * Get reader context (for caching)
     */
    virtual std::unique_ptr<IndexReaderContext> getContext() const = 0;

    // ==================== Statistics ====================

    /**
     * Total number of docs (includes deleted)
     */
    virtual int maxDoc() const = 0;

    /**
     * Number of live docs (excludes deleted)
     */
    virtual int numDocs() const = 0;

    /**
     * Check if index has deletions
     */
    virtual bool hasDeletions() const = 0;

    // ==================== Caching Support ====================

    /**
     * Cache helper for reader-level caching
     * Returns nullptr if caching not supported
     */
    virtual CacheHelper* getReaderCacheHelper() const = 0;

    // ==================== Lifecycle ====================

    /**
     * Close this reader and release resources.
     * Idempotent: safe to call multiple times.
     * Subclasses override doClose() to release specific resources.
     */
    void close() {
        bool expected = false;
        if (closed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            doClose();
        }
    }

    /**
     * Check if this reader has been closed.
     */
    bool isClosed() const { return closed_.load(std::memory_order_acquire); }

    // ==================== Deprecated Ref-Counting Shims ====================
    // These exist only for migration. shared_ptr handles lifecycle now.

    /** @deprecated No-op. shared_ptr manages lifecycle. */
    [[deprecated("shared_ptr manages lifecycle — incRef() is a no-op")]] void incRef() {}

    /** @deprecated Calls close(). Use close() directly. */
    [[deprecated("Use close() instead of decRef()")]] void decRef() { close(); }

    /** @deprecated Returns 0 if closed, 1 if open. Use isClosed(). */
    [[deprecated("Use isClosed() instead of getRefCount()")]] int getRefCount() const {
        return closed_.load(std::memory_order_acquire) ? 0 : 1;
    }

    /** @deprecated Always returns !isClosed(). */
    [[deprecated("shared_ptr manages lifecycle — tryIncRef() is deprecated")]] bool tryIncRef() {
        return !isClosed();
    }

protected:
    /**
     * Ensure reader is still usable
     */
    void ensureOpen() const {
        if (closed_.load(std::memory_order_acquire)) {
            throw AlreadyClosedException("IndexReader is closed");
        }
    }

    /**
     * Mark reader as closed
     */
    void setClosed() { closed_.store(true, std::memory_order_release); }

    /**
     * Called when closing.
     * Subclasses should override to release resources.
     * Note: setClosed() is NOT called here — close() handles the flag.
     */
    virtual void doClose() {}

private:
    std::atomic<bool> closed_{false};
};

// ==================== LeafReader (Abstract, Atomic Segment Reader) ====================

/**
 * LeafReader provides atomic read access to a single segment.
 * All doc IDs are relative to this segment [0, maxDoc()).
 *
 * Implements:
 * - Terms access via terms(field)
 * - Postings via postings(term, flags)
 * - Doc values via getNumericDocValues, etc.
 * - Stored fields via storedFieldsReader()
 * - Norms via getNormValues(field)
 *
 * Based on: org.apache.lucene.index.LeafReader
 */
class LeafReader : public IndexReader {
public:
    // ==================== Terms & Postings ====================

    /**
     * Get Terms for a field.
     * Lifetime: returned pointer valid while this LeafReader is open.
     * @return Terms or nullptr if field doesn't exist/has no terms
     */
    [[nodiscard]] virtual Terms* terms(const std::string& field) const = 0;

    // ==================== Doc Values (Column Access) ====================

    /**
     * Numeric doc values (single numeric value per doc).
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual NumericDocValues* getNumericDocValues(const std::string& field) const = 0;

    /**
     * Binary doc values (single byte[] per doc).
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual BinaryDocValues* getBinaryDocValues(const std::string& field) const = 0;

    /**
     * Sorted doc values (sorted set of byte[] values, doc→ord mapping).
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual SortedDocValues* getSortedDocValues(const std::string& field) const = 0;

    /**
     * Sorted set doc values (doc→multiple ords mapping).
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual SortedSetDocValues*
    getSortedSetDocValues(const std::string& field) const = 0;

    /**
     * Sorted numeric doc values (doc→multiple numeric values).
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual SortedNumericDocValues*
    getSortedNumericDocValues(const std::string& field) const = 0;

    // ==================== Stored Fields ====================

    /**
     * Get stored fields reader.
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual codecs::StoredFieldsReader* storedFieldsReader() const = 0;

    // ==================== Norms ====================

    /**
     * Get normalization values for field.
     * Lifetime: returned pointer valid while this LeafReader is open.
     * Returns nullptr if field doesn't have norms.
     */
    [[nodiscard]] virtual NumericDocValues* getNormValues(const std::string& field) const = 0;

    // ==================== Field Metadata ====================

    /**
     * Field infos for all indexed fields
     */
    virtual const FieldInfos& getFieldInfos() const = 0;

    /**
     * Get live docs (deleted docs bitmap).
     * Lifetime: returned pointer valid while this LeafReader is open.
     * Returns nullptr if no deletions.
     */
    [[nodiscard]] virtual const util::Bits* getLiveDocs() const = 0;

    // ==================== Points (Numeric/Geo Indexes) ====================

    /**
     * Point values for field (if indexed with PointsFormat).
     * Lifetime: returned pointer valid while this LeafReader is open.
     */
    [[nodiscard]] virtual PointValues* getPointValues(const std::string& field) const = 0;

    // ==================== Caching ====================

    /**
     * Core cache helper (for segment-level caching)
     * Invalidated only when segment is replaced
     */
    virtual CacheHelper* getCoreCacheHelper() const = 0;

    /**
     * Reader cache helper (invalidated on any change including deletes)
     */
    CacheHelper* getReaderCacheHelper() const override = 0;

    // ==================== Context ====================

    std::unique_ptr<IndexReaderContext> getContext() const override {
        return std::make_unique<LeafReaderContextWrapper>(sharedLeafPtr());
    }

    std::vector<LeafReaderContext> leaves() const override {
        return {LeafReaderContext(sharedLeafPtr(), 0, 0)};
    }

protected:
    /**
     * Get a shared_ptr<LeafReader> to this object.
     * Requires this object to be managed by a shared_ptr.
     */
    std::shared_ptr<LeafReader> sharedLeafPtr() const {
        return std::static_pointer_cast<LeafReader>(
            std::const_pointer_cast<IndexReader>(shared_from_this()));
    }
};

// ==================== CompositeReader (Abstract, Multi-Segment Reader) ====================

/**
 * CompositeReader composes multiple sub-readers.
 * Doc IDs are remapped: docID = docBase[i] + localDocID
 *
 * Based on: org.apache.lucene.index.CompositeReader
 */
class CompositeReader : public IndexReader {
public:
    /**
     * Get sequential sub-readers
     */
    virtual std::vector<std::shared_ptr<IndexReader>> getSequentialSubReaders() const = 0;

    // ==================== Statistics (Aggregated) ====================

    int maxDoc() const override {
        int max = 0;
        for (const auto& sub : getSequentialSubReaders()) {
            max += sub->maxDoc();
        }
        return max;
    }

    int numDocs() const override {
        int num = 0;
        for (const auto& sub : getSequentialSubReaders()) {
            num += sub->numDocs();
        }
        return num;
    }

    bool hasDeletions() const override {
        for (const auto& sub : getSequentialSubReaders()) {
            if (sub->hasDeletions()) {
                return true;
            }
        }
        return false;
    }

    // ==================== Context ====================

    std::unique_ptr<IndexReaderContext> getContext() const override {
        return std::make_unique<CompositeReaderContext>(const_cast<CompositeReader*>(this),
                                                        leaves());
    }

    std::vector<LeafReaderContext> leaves() const override {
        std::vector<LeafReaderContext> result;
        int docBase = 0;
        int leafOrd = 0;

        for (const auto& sub : getSequentialSubReaders()) {
            for (const auto& ctx : sub->leaves()) {
                result.push_back(LeafReaderContext(ctx.reader, docBase + ctx.docBase, leafOrd++));
            }
            // Add sub-reader's total size after processing all its leaves
            docBase += sub->maxDoc();
        }

        return result;
    }
};

}  // namespace index
}  // namespace diagon
