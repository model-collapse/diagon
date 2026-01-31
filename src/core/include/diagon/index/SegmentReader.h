// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/NumericDocValuesReader.h"
#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/StoredFieldsReader.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/IndexReader.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/index/Terms.h"
#include "diagon/store/Directory.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace index {

/**
 * SegmentReader - LeafReader implementation for a single segment
 *
 * Phase 4.3 implementation:
 * - Uses codec-specific FieldsProducer (e.g., Lucene104FieldsProducer)
 * - Supports doc values, stored fields, and norms
 * - Supports deletions via live docs
 * - Lazy loading (fields producer created on first terms() call)
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
    static std::shared_ptr<SegmentReader> open(store::Directory& dir,
                                               std::shared_ptr<SegmentInfo> si);

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

    // ==================== Doc Values ====================

    NumericDocValues* getNumericDocValues(const std::string& field) const override;

    BinaryDocValues* getBinaryDocValues(const std::string& field) const override { return nullptr; }

    SortedDocValues* getSortedDocValues(const std::string& field) const override { return nullptr; }

    SortedSetDocValues* getSortedSetDocValues(const std::string& field) const override {
        return nullptr;
    }

    SortedNumericDocValues* getSortedNumericDocValues(const std::string& field) const override {
        return nullptr;
    }

    // ==================== Stored Fields ====================

    codecs::StoredFieldsReader* storedFieldsReader() const override;

    // ==================== Norms ====================

    NumericDocValues* getNormValues(const std::string& field) const override;

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
     * Returns nullptr if no deletions, otherwise BitSet (1 = live, 0 = deleted)
     */
    const util::Bits* getLiveDocs() const override;

    // ==================== Points (Not implemented in Phase 4) ====================

    PointValues* getPointValues(const std::string& field) const override { return nullptr; }

    // ==================== Caching (Phase 5) ====================

    /**
     * Core cache helper - invalidated only when segment is replaced
     *
     * Safe to cache:
     * - Term dictionaries (immutable)
     * - Doc values (immutable)
     * - Stored fields (immutable)
     * - Field infos (immutable)
     *
     * Not invalidated by deletions.
     */
    CacheHelper* getCoreCacheHelper() const override {
        return const_cast<CacheHelper*>(&coreCacheHelper_);
    }

    /**
     * Reader cache helper - invalidated on any change (including deletions)
     *
     * Safe to cache:
     * - Document counts (numDocs, maxDoc)
     * - Statistics that depend on deletions
     *
     * Invalidated when deletions change.
     */
    CacheHelper* getReaderCacheHelper() const override {
        return const_cast<CacheHelper*>(&readerCacheHelper_);
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
     */
    int numDocs() const override {
        ensureOpen();
        return segmentInfo_->maxDoc() - segmentInfo_->delCount();
    }

    /**
     * Check if index has deletions
     */
    bool hasDeletions() const override {
        ensureOpen();
        return segmentInfo_->hasDeletions();
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
    SegmentReader(store::Directory& dir, std::shared_ptr<SegmentInfo> si);

    /**
     * Load fields producer (lazy initialization)
     */
    void loadFieldsProducer() const;

    /**
     * Load doc values reader (lazy initialization)
     */
    void loadDocValuesReader() const;

    /**
     * Load stored fields reader (lazy initialization)
     */
    void loadStoredFieldsReader() const;

    /**
     * Load live docs (lazy initialization)
     */
    void loadLiveDocs() const;

    /**
     * Load norms producer (lazy initialization)
     */
    void loadNormsProducer() const;

    // Directory containing segment files
    store::Directory& directory_;

    // Segment metadata
    std::shared_ptr<SegmentInfo> segmentInfo_;

    // Fields producer (segment-wide, lazy loaded)
    // Phase 4.3: Uses codec-specific producer (e.g., Lucene104FieldsProducer)
    mutable std::unique_ptr<codecs::FieldsProducer> fieldsProducer_;

    // Cached Terms objects
    mutable std::unordered_map<std::string, std::unique_ptr<Terms>> termsCache_;

    // Doc values reader (lazy loaded)
    mutable std::unique_ptr<codecs::NumericDocValuesReader> docValuesReader_;

    // Cached NumericDocValues objects
    mutable std::unordered_map<std::string, std::unique_ptr<NumericDocValues>>
        numericDocValuesCache_;

    // Stored fields reader (lazy loaded)
    mutable std::unique_ptr<codecs::StoredFieldsReader> storedFieldsReader_;

    // Norms producer (lazy loaded)
    mutable std::unique_ptr<codecs::NormsProducer> normsProducer_;

    // Cached norms objects
    mutable std::unordered_map<std::string, std::unique_ptr<NumericDocValues>> normsCache_;

    // Live docs (lazy loaded) - nullptr if no deletions
    mutable std::unique_ptr<util::BitSet> liveDocs_;

    // Flag to track if we've attempted to load live docs
    mutable bool liveDocsLoaded_{false};

    // Cache helpers (Phase 5)
    // Core cache helper: invalidated only when segment is replaced
    CacheHelper coreCacheHelper_;

    // Reader cache helper: invalidated when deletions change
    CacheHelper readerCacheHelper_;
};

}  // namespace index
}  // namespace diagon
