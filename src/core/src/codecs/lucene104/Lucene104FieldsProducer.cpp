// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104FieldsProducer.h"

#include "diagon/codecs/SegmentState.h"
#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
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
                   Lucene104PostingsReader* postingsReader, const index::FieldInfo* fieldInfo,
                   int64_t sumTotalTermFreq, int64_t sumDocFreq, int docCount)
        : reader_(reader)
        , postingsReader_(postingsReader)
        , fieldInfo_(fieldInfo)
        , sumTotalTermFreq_(sumTotalTermFreq)
        , sumDocFreq_(sumDocFreq)
        , docCount_(docCount) {}

    std::unique_ptr<index::TermsEnum> iterator() const override {
        auto termsEnum = reader_->iterator();

        // Cast to SegmentTermsEnum to set PostingsReader
        auto* segmentEnum = dynamic_cast<blocktree::SegmentTermsEnum*>(termsEnum.get());
        if (segmentEnum && postingsReader_ && fieldInfo_) {
            segmentEnum->setPostingsReader(postingsReader_, fieldInfo_);
        }

        return termsEnum;
    }

    int64_t size() const override { return reader_->getNumTerms(); }

    int getDocCount() const override { return docCount_; }

    int64_t getSumTotalTermFreq() const override { return sumTotalTermFreq_; }

    int64_t getSumDocFreq() const override { return sumDocFreq_; }

private:
    std::shared_ptr<blocktree::BlockTreeTermsReader> reader_;
    Lucene104PostingsReader* postingsReader_;
    const index::FieldInfo* fieldInfo_;
    int64_t sumTotalTermFreq_;
    int64_t sumDocFreq_;
    int docCount_;
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

    // Read field metadata from .tmd file
    std::string tmdFile = segmentName_ + ".tmd";
    try {
        auto tmdInput = state.directory->openInput(tmdFile, store::IOContext::READ);

        // Read number of fields
        int numFields = tmdInput->readVInt();

        // Read each field's metadata
        for (int i = 0; i < numFields; i++) {
            std::string fieldName = tmdInput->readString();

            FieldMetadata metadata;
            metadata.numTerms = tmdInput->readVLong();
            metadata.sumTotalTermFreq = tmdInput->readVLong();
            metadata.sumDocFreq = tmdInput->readVLong();
            metadata.docCount = tmdInput->readVInt();

            fieldMetadata_[fieldName] = metadata;
        }

        // tmdInput will be automatically closed when it goes out of scope
    } catch (const std::exception& e) {
        // .tmd file might not exist for old indexes, that's ok
        std::cerr << "Warning: Could not read field metadata: " << e.what() << std::endl;
    }

    // Create PostingsReader for reading actual postings
    postingsReader_ = std::make_unique<Lucene104PostingsReader>(state);

    // Open .doc file and set input for PostingsReader
    std::string docFile = segmentName_ + ".doc";
    auto docInput = state.directory->openInput(docFile, store::IOContext::READ);
    postingsReader_->setInput(std::move(docInput));

    // Open .skp file if it exists (may not exist for small indexes)
    // Skip file is optional - used for WAND impacts optimization
    try {
        std::string skipFile = segmentName_ + ".skp";
        auto skipInput = state.directory->openInput(skipFile, store::IOContext::READ);
        postingsReader_->setSkipInput(std::move(skipInput));
    } catch (const std::exception& e) {
        // Skip file doesn't exist - that's OK for small indexes
        // PostingsReader will work without skip data (just won't use WAND optimization)
    }
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

    // Get field metadata (use defaults if not found)
    int64_t sumTotalTermFreq = -1;
    int64_t sumDocFreq = -1;
    int docCount = 0;
    auto metaIt = fieldMetadata_.find(field);
    if (metaIt != fieldMetadata_.end()) {
        sumTotalTermFreq = metaIt->second.sumTotalTermFreq;
        sumDocFreq = metaIt->second.sumDocFreq;
        docCount = metaIt->second.docCount;
    }

    // Check if we already have a reader for this field
    auto it = fieldReaders_.find(field);
    if (it != fieldReaders_.end()) {
        return std::make_unique<BlockTreeTerms>(it->second.reader, postingsReader_.get(), fieldInfo,
                                                sumTotalTermFreq, sumDocFreq, docCount);
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

    // Return Terms wrapper with PostingsReader wired up and field metadata
    return std::make_unique<BlockTreeTerms>(reader, postingsReader_.get(), fieldInfo,
                                            sumTotalTermFreq, sumDocFreq, docCount);
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
