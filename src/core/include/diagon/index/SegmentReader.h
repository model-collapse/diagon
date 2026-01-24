// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/IndexReader.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/Terms.h"
#include "diagon/codecs/SimpleFieldsProducer.h"
#include "diagon/store/Directory.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace index {

/**
 * SegmentReader - LeafReader implementation for a single segment
 *
 * Phase 4 implementation:
 * - Uses SimpleFieldsProducer to read .post files
 * - No doc values, stored fields, or norms yet
 * - No deletions (numDocs == maxDoc)
 * - Eager loading (all data loaded on open)
 *
 * Thread-safe for concurrent reads after construction.
 *
 * Based on: org.apache.lucene.index.SegmentReader
 */
class SegmentReader : public LeafReader {
public:
    /**
     * Open a segment reader
     *
     * @param dir Directory containing segment files
     * @param si SegmentInfo for the segment
     * @return SegmentReader instance
     */
    static std::shared_ptr<SegmentReader> open(
        store::Directory& dir,
        std::shared_ptr<SegmentInfo> si
    );

    /**
     * Destructor
     */
    ~SegmentReader() override;

    // ==================== Terms & Postings ====================

    /**
     * Get Terms for a field
     * Returns nullptr if field doesn't exist
     */
    Terms* terms(const std::string& field) const override;

    // ==================== Doc Values (Not implemented in Phase 4) ====================

    NumericDocValues* getNumericDocValues(const std::string& field) const override {
        return nullptr;
    }

    BinaryDocValues* getBinaryDocValues(const std::string& field) const override {
        return nullptr;
    }

    SortedDocValues* getSortedDocValues(const std::string& field) const override {
        return nullptr;
    }

    SortedSetDocValues* getSortedSetDocValues(const std::string& field) const override {
        return nullptr;
    }

    SortedNumericDocValues* getSortedNumericDocValues(const std::string& field) const override {
        return nullptr;
    }

    // ==================== Stored Fields (Not implemented in Phase 4) ====================

    StoredFieldsReader* storedFieldsReader() const override {
        return nullptr;
    }

    // ==================== Norms (Not implemented in Phase 4) ====================

    NumericDocValues* getNormValues(const std::string& field) const override {
        return nullptr;
    }

    // ==================== Field Metadata ====================

    /**
     * Get field infos
     */
    const FieldInfos& getFieldInfos() const override {
        ensureOpen();
        return segmentInfo_->fieldInfos();
    }

    /**
     * Get live docs (deleted docs bitmap)
     * Returns nullptr - no deletions in Phase 4
     */
    const Bits* getLiveDocs() const override {
        return nullptr;
    }

    // ==================== Points (Not implemented in Phase 4) ====================

    PointValues* getPointValues(const std::string& field) const override {
        return nullptr;
    }

    // ==================== Caching (Not implemented in Phase 4) ====================

    CacheHelper* getCoreCacheHelper() const override {
        return nullptr;
    }

    CacheHelper* getReaderCacheHelper() const override {
        return nullptr;
    }

    // ==================== Statistics ====================

    /**
     * Total number of docs (includes deleted)
     */
    int maxDoc() const override {
        ensureOpen();
        return segmentInfo_->maxDoc();
    }

    /**
     * Number of live docs (excludes deleted)
     * Phase 4: No deletions, so equals maxDoc
     */
    int numDocs() const override {
        ensureOpen();
        return segmentInfo_->maxDoc();
    }

    /**
     * Check if index has deletions
     * Phase 4: Always false
     */
    bool hasDeletions() const override {
        ensureOpen();
        return false;
    }

    // ==================== Segment Info ====================

    /**
     * Get segment info
     */
    std::shared_ptr<SegmentInfo> getSegmentInfo() const {
        ensureOpen();
        return segmentInfo_;
    }

    /**
     * Get segment name
     */
    const std::string& getSegmentName() const {
        ensureOpen();
        return segmentInfo_->name();
    }

protected:
    /**
     * Called when closing (refCount reaches 0)
     */
    void doClose() override;

private:
    /**
     * Private constructor - use open() factory method
     */
    SegmentReader(
        store::Directory& dir,
        std::shared_ptr<SegmentInfo> si
    );

    /**
     * Load fields producer for a field
     */
    void loadFieldsProducer(const std::string& field) const;

    // Directory containing segment files
    store::Directory& directory_;

    // Segment metadata
    std::shared_ptr<SegmentInfo> segmentInfo_;

    // Fields producers (one per field, lazy loaded)
    // Phase 4: Eager loading, but using mutable for lazy pattern compatibility
    mutable std::unordered_map<std::string, std::unique_ptr<codecs::SimpleFieldsProducer>> fieldsProducers_;

    // Cached Terms objects
    mutable std::unordered_map<std::string, std::unique_ptr<Terms>> termsCache_;
};

}  // namespace index
}  // namespace diagon
