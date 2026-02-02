// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentReader.h"

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/SegmentState.h"
#include "diagon/util/Exceptions.h"

#include <atomic>
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

    // Increment ref count (starts at 1 from IndexReader)
    // No need to incRef() - already at 1 from constructor

    return reader;
}

// ==================== Constructor / Destructor ====================

SegmentReader::SegmentReader(Directory& dir, std::shared_ptr<SegmentInfo> si)
    : directory_(dir)
    , segmentInfo_(si) {
    // Phase 4: Constructor completes immediately
    // Fields producers are loaded lazily on first terms() call
}

SegmentReader::~SegmentReader() {
    // Ensure proper cleanup
    if (getRefCount() > 0) {
        doClose();
    }
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
            termsCache_[field] = std::move(terms);
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

    // FIX: Do NOT cache NumericDocValues iterators - they are stateful!
    // Creating fresh iterators for each query prevents:
    // 1. State persistence across queries (docID_ remaining at last position)
    // 2. Integer overflow when nextDoc() increments past NO_MORE_DOCS
    //
    // Performance note: This trades memory allocation cost for correctness.
    // The cost is small since we're only loading int64 arrays from disk once
    // (the DocValuesReader itself is still cached).

    // Load doc values reader if not already loaded
    loadDocValuesReader();

    // Get fresh NumericDocValues iterator for this query
    if (docValuesReader_) {
        auto dv = docValuesReader_->getNumeric(field);
        if (dv) {
            // Store in cache for lifecycle management (will be freed when reader closes)
            // Use a unique key to prevent reuse
            static std::atomic<uint64_t> requestId{0};
            std::string cacheKey = field + "_" + std::to_string(requestId.fetch_add(1));
            NumericDocValues* dvPtr = dv.get();
            numericDocValuesCache_[cacheKey] = std::move(dv);
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

// ==================== Internal Methods ====================

void SegmentReader::loadFieldsProducer() const {
    // Check if already loaded
    if (fieldsProducer_) {
        return;
    }


    // Phase 4.3: Get codec and create appropriate FieldsProducer
    try {
        // Get codec name from segment info
        std::string codecName = segmentInfo_->codecName();

        auto& codec = codecs::Codec::forName(codecName);
        auto& postingsFormat = codec.postingsFormat();

        // Create segment read state (using index::SegmentReadState)
        std::string segmentName = segmentInfo_->name();

        SegmentReadState readState(&directory_, segmentName, segmentInfo_->maxDoc(),
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
        std::string segmentName = segmentInfo_->name();
        auto dataInput = directory_.openInput(segmentName + ".dvd", IOContext::READ);
        auto metaInput = directory_.openInput(segmentName + ".dvm", IOContext::READ);

        docValuesReader_ =
            std::make_unique<NumericDocValuesReader>(std::move(dataInput), std::move(metaInput));
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
        std::string segmentName = segmentInfo_->name();
        storedFieldsReader_ = std::make_unique<codecs::StoredFieldsReader>(
            &directory_, segmentName, segmentInfo_->fieldInfos());
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

        // Create segment read state
        std::string segmentName = segmentInfo_->name();
        SegmentReadState readState(&directory_, segmentName, segmentInfo_->maxDoc(),
                                   segmentInfo_->fieldInfos(), "");

        // Create norms producer
        normsProducer_ = normsFormat.normsProducer(readState);
    } catch (const std::exception& e) {
        // Norms files don't exist or codec doesn't support norms - that's OK
        // Leave normsProducer_ as nullptr
    }
}

// ==================== Lifecycle ====================

void SegmentReader::doClose() {
    // Mark as closed
    setClosed();

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

    // Clear live docs
    liveDocs_.reset();
    liveDocsLoaded_ = false;
}

}  // namespace index
}  // namespace diagon
