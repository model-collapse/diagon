// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentReader.h"

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/SegmentState.h"
#include "diagon/store/CompoundDirectory.h"
#include "diagon/util/Exceptions.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <iostream>

namespace diagon {
namespace index {

using namespace diagon::codecs;
using namespace diagon::store;
using namespace diagon::util;

// ==================== Factory Method ====================

std::shared_ptr<SegmentReader> SegmentReader::open(Directory& dir,
                                                   std::shared_ptr<SegmentInfo> si) {
    // Constructor is private, use std::shared_ptr constructor hack
    auto reader = std::shared_ptr<SegmentReader>(new SegmentReader(dir, si));
    return reader;
}

// ==================== Constructor / Destructor ====================

SegmentReader::SegmentReader(Directory& dir, std::shared_ptr<SegmentInfo> si)
    : directory_(dir)
    , segmentInfo_(si) {
    // If segment uses compound file format, open CompoundDirectory
    if (si->getUseCompoundFile()) {
        try {
            compoundDirectory_ = store::CompoundDirectory::open(dir, si->name());
        } catch (const IOException&) {
            // Compound files might not exist (e.g., old index or mid-migration)
            // Fall back to raw directory
        }
    }
    // Fields producers are loaded lazily on first terms() call
}

SegmentReader::~SegmentReader() {
    // Must call close() here — virtual dispatch in ~IndexReader() would call
    // IndexReader::doClose() (empty), not SegmentReader::doClose().
    close();
}

// ==================== Terms Access ====================

Terms* SegmentReader::terms(const std::string& field) const {
    ensureOpen();

    // Check if field exists in FieldInfos
    const FieldInfo* fieldInfo = segmentInfo_->fieldInfos().fieldInfo(field);
    if (!fieldInfo) {
        return nullptr;  // Field doesn't exist
    }

    if (!fieldInfo->hasPostings()) {
        return nullptr;  // Field is not indexed
    }

    // Check if we already have a cached Terms object
    auto it = termsCache_.find(field);
    if (it != termsCache_.end()) {
        return it->second.get();
    }

    // Load fields producer if not already loaded
    loadFieldsProducer();

    // Get Terms from producer
    if (fieldsProducer_) {
        auto terms = fieldsProducer_->terms(field);
        if (terms) {
            Terms* termsPtr = terms.get();
            size_t approxBytes = sizeof(Terms) + field.size();
            termsCache_[field] = std::move(terms);
            cacheMemoryUsed_.fetch_add(approxBytes, std::memory_order_relaxed);
            assert(termsCache_.size() <= 1000 && "termsCache_ growing unbounded — possible leak");
            if (cacheMemoryUsed_.load(std::memory_order_relaxed) > cacheMemoryBudget_) {
                fprintf(stderr, "WARNING: SegmentReader cache memory %zu exceeds budget %zu\n",
                        cacheMemoryUsed_.load(std::memory_order_relaxed), cacheMemoryBudget_);
            }
            return termsPtr;
        } else {
        }
    } else {
    }

    return nullptr;
}

// ==================== Doc Values Access ====================

NumericDocValues* SegmentReader::getNumericDocValues(const std::string& field) const {
    ensureOpen();

    // Check if field exists in FieldInfos
    const FieldInfo* fieldInfo = segmentInfo_->fieldInfos().fieldInfo(field);
    if (!fieldInfo) {
        return nullptr;  // Field doesn't exist
    }

    if (fieldInfo->docValuesType != DocValuesType::NUMERIC) {
        return nullptr;  // Field doesn't have numeric doc values
    }

    // Load doc values reader if not already loaded
    loadDocValuesReader();

    // Create a fresh iterator each call (iterators are stateful — docID position).
    // Cache by field name so the previous entry is replaced and freed.
    if (docValuesReader_) {
        auto dv = docValuesReader_->getNumeric(field);
        if (dv) {
            NumericDocValues* dvPtr = dv.get();
            bool isNew = (numericDocValuesCache_.find(field) == numericDocValuesCache_.end());
            numericDocValuesCache_[field] = std::move(dv);
            if (isNew) {
                cacheMemoryUsed_.fetch_add(sizeof(NumericDocValues) + field.size(),
                                           std::memory_order_relaxed);
            }
            assert(numericDocValuesCache_.size() <= 1000 &&
                   "numericDocValuesCache_ growing unbounded — possible leak");
            return dvPtr;
        }
    }

    return nullptr;
}

// ==================== Norms Access ====================

NumericDocValues* SegmentReader::getNormValues(const std::string& field) const {
    ensureOpen();

    // Check if field exists in FieldInfos
    const FieldInfo* fieldInfo = segmentInfo_->fieldInfos().fieldInfo(field);
    if (!fieldInfo) {
        return nullptr;  // Field doesn't exist
    }

    // Check if field has norms
    if (fieldInfo->omitNorms || fieldInfo->indexOptions == IndexOptions::NONE) {
        return nullptr;  // Field doesn't have norms
    }

    // Check if we already have cached norms
    auto it = normsCache_.find(field);
    if (it != normsCache_.end()) {
        return it->second.get();
    }

    // Load norms producer if not already loaded
    loadNormsProducer();

    // Get norms from producer
    if (normsProducer_) {
        try {
            auto norms = normsProducer_->getNorms(*fieldInfo);
            if (norms) {
                NumericDocValues* normsPtr = norms.get();
                normsCache_[field] = std::move(norms);
                cacheMemoryUsed_.fetch_add(sizeof(NumericDocValues) + field.size(),
                                           std::memory_order_relaxed);
                assert(normsCache_.size() <= 1000 &&
                       "normsCache_ growing unbounded — possible leak");
                return normsPtr;
            }
        } catch (const std::exception& e) {
            // Failed to load norms - return nullptr
        }
    }

    return nullptr;
}

// ==================== Stored Fields Access ====================

codecs::StoredFieldsReader* SegmentReader::storedFieldsReader() const {
    ensureOpen();

    // Load stored fields reader if not already loaded
    loadStoredFieldsReader();

    return storedFieldsReader_.get();
}

// ==================== Live Docs Access ====================

const util::Bits* SegmentReader::getLiveDocs() const {
    ensureOpen();

    // Load live docs if not already loaded
    loadLiveDocs();

    return liveDocs_.get();
}

// ==================== Points Access ====================

PointValues* SegmentReader::getPointValues(const std::string& field) const {
    ensureOpen();

    // Check if field exists and has point values
    const FieldInfo* fieldInfo = segmentInfo_->fieldInfos().fieldInfo(field);
    if (!fieldInfo || fieldInfo->pointDimensionCount == 0) {
        return nullptr;
    }

    // Load points readers if not already loaded
    loadPointsReader();

    auto it = pointsReaders_.find(field);
    if (it != pointsReaders_.end()) {
        return it->second.get();
    }

    return nullptr;
}

// ==================== Internal Methods ====================

void SegmentReader::loadFieldsProducer() const {
    // Check if already loaded
    if (fieldsProducer_) {
        return;
    }

    // Phase 4.3: Get codec and create appropriate FieldsProducer
    std::string segmentName = segmentInfo_->name();
    try {
        // Get codec name from segment info
        std::string codecName = segmentInfo_->codecName();

        auto& codec = codecs::Codec::forName(codecName);
        auto& postingsFormat = codec.postingsFormat();

        // Use compound directory if available, otherwise raw directory
        auto& dir = getDirectory();

        // Create segment read state (using index::SegmentReadState)
        SegmentReadState readState(&dir, segmentName, segmentInfo_->maxDoc(),
                                   segmentInfo_->fieldInfos(), "");

        // Create fields producer using codec
        fieldsProducer_ = postingsFormat.fieldsProducer(readState);
    } catch (const std::exception& e) {
        // Failed to load fields producer - leave as nullptr
        // This can happen if postings files don't exist
    }
}

void SegmentReader::loadDocValuesReader() const {
    // Check if already loaded
    if (docValuesReader_) {
        return;
    }

    // Try to open doc values files
    try {
        auto& dir = getDirectory();
        std::string segmentName = segmentInfo_->name();
        auto dataInput = dir.openInput(segmentName + ".dvd", IOContext::READ);
        auto metaInput = dir.openInput(segmentName + ".dvm", IOContext::READ);

        docValuesReader_ = std::make_unique<NumericDocValuesReader>(std::move(dataInput),
                                                                    std::move(metaInput));
    } catch (const std::exception& e) {
        // Doc values files don't exist - that's OK
        // Leave docValuesReader_ as nullptr
    }
}

void SegmentReader::loadStoredFieldsReader() const {
    // Check if already loaded
    if (storedFieldsReader_) {
        return;
    }

    // Try to open stored fields files
    try {
        auto& dir = getDirectory();
        std::string segmentName = segmentInfo_->name();
        storedFieldsReader_ = std::make_unique<codecs::StoredFieldsReader>(
            &dir, segmentName, segmentInfo_->fieldInfos());
    } catch (const std::exception& e) {
        // Stored fields files don't exist - that's OK
        // Leave storedFieldsReader_ as nullptr
    }
}

void SegmentReader::loadLiveDocs() const {
    // Check if already attempted to load
    if (liveDocsLoaded_) {
        return;
    }

    liveDocsLoaded_ = true;

    // Check if segment has deletions
    if (!segmentInfo_->hasDeletions()) {
        // No deletions - liveDocs_ stays nullptr
        return;
    }

    // Load live docs from .liv file
    // Note: .liv files are NOT stored in compound files (Lucene convention)
    try {
        std::string segmentName = segmentInfo_->name();
        int maxDoc = segmentInfo_->maxDoc();

        codecs::LiveDocsFormat format;
        liveDocs_ = format.readLiveDocs(directory_, segmentName, maxDoc);
    } catch (const std::exception& e) {
        // Failed to load live docs - leave as nullptr
        // This could happen if .liv file is missing
    }
}

void SegmentReader::loadNormsProducer() const {
    // Check if already loaded
    if (normsProducer_) {
        return;
    }

    // Get codec and create norms producer
    try {
        // Get codec name from segment info
        std::string codecName = segmentInfo_->codecName();
        auto& codec = codecs::Codec::forName(codecName);
        auto& normsFormat = codec.normsFormat();

        // Use compound directory if available
        auto& dir = getDirectory();

        // Create segment read state
        std::string segmentName = segmentInfo_->name();
        SegmentReadState readState(&dir, segmentName, segmentInfo_->maxDoc(),
                                   segmentInfo_->fieldInfos(), "");

        // Create norms producer
        normsProducer_ = normsFormat.normsProducer(readState);
    } catch (const std::exception& e) {
        // Norms files don't exist or codec doesn't support norms - that's OK
        // Leave normsProducer_ as nullptr
    }
}

void SegmentReader::loadPointsReader() const {
    if (pointsLoaded_) {
        return;
    }
    pointsLoaded_ = true;

    try {
        auto& dir = getDirectory();
        std::string segmentName = segmentInfo_->name();

        auto kdmInput = dir.openInput(segmentName + ".kdm", IOContext::READ);
        auto kdiInput = dir.openInput(segmentName + ".kdi", IOContext::READ);
        auto kddInput = dir.openInput(segmentName + ".kdd", IOContext::READ);

        pointsReaders_ = codecs::BKDReader::loadFields(*kdmInput, *kdiInput, *kddInput);
    } catch (const std::exception&) {
        // Points files don't exist — that's OK
    }
}

// ==================== Lifecycle ====================

void SegmentReader::doClose() {
    // Clear fields producer and cache
    if (fieldsProducer_) {
        fieldsProducer_->close();
        fieldsProducer_.reset();
    }
    termsCache_.clear();

    // Clear doc values
    docValuesReader_.reset();
    numericDocValuesCache_.clear();

    // Clear stored fields reader
    storedFieldsReader_.reset();

    // Clear norms
    normsProducer_.reset();
    normsCache_.clear();

    // Clear points
    pointsReaders_.clear();
    pointsLoaded_ = false;

    // Clear live docs
    liveDocs_.reset();
    liveDocsLoaded_ = false;

    // Close compound directory
    if (compoundDirectory_) {
        compoundDirectory_->close();
        compoundDirectory_.reset();
    }

    // Reset memory tracking
    cacheMemoryUsed_.store(0, std::memory_order_relaxed);
}

}  // namespace index
}  // namespace diagon
