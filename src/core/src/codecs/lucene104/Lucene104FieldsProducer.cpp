// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104FieldsProducer.h"

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/Exceptions.h"

#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104FieldsProducer::Lucene104FieldsProducer(::diagon::index::SegmentReadState& state)
    : state_(state) {

    // Open .tim file (term dictionary)
    std::string timFileName = state.segmentName;
    if (!state.segmentSuffix.empty()) {
        timFileName += "_" + state.segmentSuffix;
    }
    timFileName += ".tim";

    try {
        timIn_ = state.directory->openInput(timFileName, store::IOContext::READ);
    } catch (const std::exception& e) {
        throw IOException("Failed to open .tim file: " + timFileName + ": " + e.what());
    }

    // Open .tip file (FST index)
    std::string tipFileName = state.segmentName;
    if (!state.segmentSuffix.empty()) {
        tipFileName += "_" + state.segmentSuffix;
    }
    tipFileName += ".tip";

    try {
        tipIn_ = state.directory->openInput(tipFileName, store::IOContext::READ);
    } catch (const std::exception& e) {
        throw IOException("Failed to open .tip file: " + tipFileName + ": " + e.what());
    }

    // Create postings reader
    postingsReader_ = std::make_unique<Lucene104PostingsReader>(state);
}

Lucene104FieldsProducer::~Lucene104FieldsProducer() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

blocktree::BlockTreeTermsReader* Lucene104FieldsProducer::getTermsReader(
    const std::string& field) {
    // Check if already cached
    auto it = termsReaders_.find(field);
    if (it != termsReaders_.end()) {
        return it->second.get();
    }

    // Get field info
    const auto* fieldInfo = state_.fieldInfos.fieldInfo(field);
    if (!fieldInfo) {
        return nullptr;  // Field doesn't exist
    }

    // Check if field has inverted index
    if (fieldInfo->indexOptions == ::diagon::index::IndexOptions::NONE) {
        return nullptr;  // Field not indexed
    }

    // Create new terms reader
    auto reader = std::make_unique<blocktree::BlockTreeTermsReader>(
        timIn_.get(), tipIn_.get(), *fieldInfo);

    auto* ptr = reader.get();
    termsReaders_[field] = std::move(reader);

    return ptr;
}

std::unique_ptr<::diagon::index::Terms> Lucene104FieldsProducer::terms(const std::string& field) {
    if (closed_) {
        throw AlreadyClosedException("FieldsProducer already closed");
    }

    auto* termsReader = getTermsReader(field);
    if (!termsReader) {
        return nullptr;  // Field doesn't exist or not indexed
    }

    // Get field info
    const auto* fieldInfo = state_.fieldInfos.fieldInfo(field);

    return std::make_unique<Lucene104Terms>(termsReader, postingsReader_.get(), fieldInfo);
}

void Lucene104FieldsProducer::close() {
    if (closed_) {
        return;
    }

    try {
        // Close input files
        timIn_.reset();
        tipIn_.reset();

        // Close postings reader
        if (postingsReader_) {
            postingsReader_->close();
            postingsReader_.reset();
        }

        // Clear terms readers
        termsReaders_.clear();

        closed_ = true;
    } catch (const std::exception& e) {
        throw IOException("Failed to close FieldsProducer: " + std::string(e.what()));
    }
}

// ==================== Lucene104Terms ====================

Lucene104Terms::Lucene104Terms(blocktree::BlockTreeTermsReader* termsReader,
                               Lucene104PostingsReader* postingsReader,
                               const ::diagon::index::FieldInfo* fieldInfo)
    : termsReader_(termsReader)
    , postingsReader_(postingsReader)
    , fieldInfo_(fieldInfo) {}

std::unique_ptr<::diagon::index::TermsEnum> Lucene104Terms::iterator() const {
    // Get SegmentTermsEnum from BlockTreeTermsReader
    auto termsEnum = termsReader_->iterator();

    // Cast to SegmentTermsEnum to set postings reader
    auto* segmentEnum = dynamic_cast<blocktree::SegmentTermsEnum*>(termsEnum.get());
    if (segmentEnum) {
        segmentEnum->setPostingsReader(postingsReader_, fieldInfo_);
    }

    return termsEnum;
}

int64_t Lucene104Terms::size() const {
    return termsReader_->getNumTerms();
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
