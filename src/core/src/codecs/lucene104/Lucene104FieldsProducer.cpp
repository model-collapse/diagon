// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104FieldsProducer.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"

#include "diagon/codecs/SegmentState.h"
#include "diagon/index/Terms.h"
#include "diagon/store/IOContext.h"

#include <iostream>

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== BlockTreeTerms Wrapper ====================

/**
 * Terms wrapper around BlockTreeTermsReader.
 * Wires up PostingsReader to TermsEnum.
 */
class BlockTreeTerms : public index::Terms {
public:
    BlockTreeTerms(std::shared_ptr<blocktree::BlockTreeTermsReader> reader,
                   Lucene104PostingsReader* postingsReader,
                   const index::FieldInfo* fieldInfo)
        : reader_(reader)
        , postingsReader_(postingsReader)
        , fieldInfo_(fieldInfo) {}

    std::unique_ptr<index::TermsEnum> iterator() const override {
        auto termsEnum = reader_->iterator();
        
        // Cast to SegmentTermsEnum to set PostingsReader
        auto* segmentEnum = dynamic_cast<blocktree::SegmentTermsEnum*>(termsEnum.get());
        if (segmentEnum && postingsReader_ && fieldInfo_) {
            segmentEnum->setPostingsReader(postingsReader_, fieldInfo_);
        }
        
        return termsEnum;
    }

    int64_t size() const override {
        return reader_->getNumTerms();
    }

    int getDocCount() const override {
        // TODO: Track doc count in BlockTreeTermsReader
        return static_cast<int>(reader_->getNumTerms());
    }

    int64_t getSumTotalTermFreq() const override {
        return -1;  // Unknown
    }

    int64_t getSumDocFreq() const override {
        return -1;  // Unknown
    }

private:
    std::shared_ptr<blocktree::BlockTreeTermsReader> reader_;
    Lucene104PostingsReader* postingsReader_;
    const index::FieldInfo* fieldInfo_;
};

// ==================== Lucene104FieldsProducer Implementation ====================

Lucene104FieldsProducer::Lucene104FieldsProducer(index::SegmentReadState& state)
    : segmentName_(state.segmentName)
    , fieldInfos_(state.fieldInfos) {


    // Open .tim file (term blocks)
    std::string timFile = segmentName_ + ".tim";
    timInput_ = state.directory->openInput(timFile, store::IOContext::READ);

    // Open .tip file (term index FST)
    std::string tipFile = segmentName_ + ".tip";
    tipInput_ = state.directory->openInput(tipFile, store::IOContext::READ);

    // Create PostingsReader for reading actual postings
    postingsReader_ = std::make_unique<Lucene104PostingsReader>(state);

    // Open .doc file and set input for PostingsReader
    std::string docFile = segmentName_ + ".doc";
    auto docInput = state.directory->openInput(docFile, store::IOContext::READ);
    postingsReader_->setInput(std::move(docInput));
}

Lucene104FieldsProducer::~Lucene104FieldsProducer() {
    close();
}

std::unique_ptr<index::Terms> Lucene104FieldsProducer::terms(const std::string& field) {

    // Check if field exists in FieldInfos
    const index::FieldInfo* fieldInfo = fieldInfos_.fieldInfo(field);
    if (!fieldInfo) {
        return nullptr;
    }

    if (!fieldInfo->hasPostings()) {
        return nullptr;
    }

    // Check if we already have a reader for this field
    auto it = fieldReaders_.find(field);
    if (it != fieldReaders_.end()) {
        return std::make_unique<BlockTreeTerms>(it->second.reader, postingsReader_.get(), fieldInfo);
    }

    // Clone inputs so each field reader has independent file pointers
    // CRITICAL: Multiple BlockTreeTermsReader instances cannot share the same IndexInput
    // because they would interfere with each other's file pointer positions
    auto timInputClone = timInput_->clone();
    auto tipInputClone = tipInput_->clone();

    // Create new BlockTreeTermsReader for this field with cloned inputs
    auto reader = std::make_shared<blocktree::BlockTreeTermsReader>(
        timInputClone.get(), tipInputClone.get(), *fieldInfo);

    // Cache the reader along with its input clones (to keep them alive)
    FieldReaderHolder holder;
    holder.reader = reader;
    holder.timInputClone = std::move(timInputClone);
    holder.tipInputClone = std::move(tipInputClone);
    fieldReaders_[field] = std::move(holder);

    // Return Terms wrapper with PostingsReader wired up
    return std::make_unique<BlockTreeTerms>(reader, postingsReader_.get(), fieldInfo);
}

void Lucene104FieldsProducer::checkIntegrity() {
    // TODO: Implement checksum validation
}

void Lucene104FieldsProducer::close() {
    fieldReaders_.clear();
    if (postingsReader_) {
        postingsReader_->close();
        postingsReader_.reset();
    }
    timInput_.reset();
    tipInput_.reset();
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
