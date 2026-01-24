// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentReader.h"

#include "diagon/util/Exceptions.h"

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
    loadFieldsProducer(field);

    // Get Terms from producer
    auto producerIt = fieldsProducers_.find(field);
    if (producerIt != fieldsProducers_.end()) {
        auto terms = producerIt->second->terms();
        Terms* termsPtr = terms.get();
        termsCache_[field] = std::move(terms);
        return termsPtr;
    }

    return nullptr;
}

// ==================== Internal Methods ====================

void SegmentReader::loadFieldsProducer(const std::string& field) const {
    // Check if already loaded
    if (fieldsProducers_.find(field) != fieldsProducers_.end()) {
        return;
    }

    // Phase 4: Create SimpleFieldsProducer
    // Note: SimpleFieldsProducer reads <segment>.post file with a single field
    auto producer = std::make_unique<SimpleFieldsProducer>(directory_, segmentInfo_->name(), field);

    fieldsProducers_[field] = std::move(producer);
}

// ==================== Lifecycle ====================

void SegmentReader::doClose() {
    // Mark as closed
    setClosed();

    // Clear producers
    fieldsProducers_.clear();
    termsCache_.clear();
}

}  // namespace index
}  // namespace diagon
