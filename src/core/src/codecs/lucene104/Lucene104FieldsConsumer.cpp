// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104FieldsConsumer.h"

#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/BytesRef.h"
#include "diagon/util/Exceptions.h"

#include <iostream>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104FieldsConsumer::Lucene104FieldsConsumer(index::SegmentWriteState& state)
    : state_(state) {
    // Create postings writer
    postingsWriter_ = std::make_unique<Lucene104PostingsWriter>(state);

    // Create term dictionary outputs
    std::string timFileName = state.segmentName;
    std::string tipFileName = state.segmentName;
    if (!state.segmentSuffix.empty()) {
        timFileName += "_" + state.segmentSuffix;
        tipFileName += "_" + state.segmentSuffix;
    }
    timFileName += ".tim";
    tipFileName += ".tip";

    timOut_ = state.directory->createOutput(timFileName, store::IOContext::DEFAULT);
    tipOut_ = state.directory->createOutput(tipFileName, store::IOContext::DEFAULT);

    files_.push_back(timFileName);
    files_.push_back(tipFileName);
}

Lucene104FieldsConsumer::~Lucene104FieldsConsumer() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

void Lucene104FieldsConsumer::write(index::Fields& fields, codecs::NormsProducer* norms) {
    if (closed_) {
        throw AlreadyClosedException("FieldsConsumer already closed");
    }

    // Iterate over all fields
    auto fieldIterator = fields.iterator();

    int fieldCount = 0;
    while (fieldIterator->hasNext()) {
        std::string fieldName = fieldIterator->next();
        fieldCount++;

        // Get terms for this field
        auto terms = fields.terms(fieldName);
        if (!terms) {
            continue;  // Field has no terms
        }

        // Write this field with norms
        writeField(fieldName, *terms, norms);
    }
}

void Lucene104FieldsConsumer::writeField(const std::string& fieldName, index::Terms& terms,
                                         codecs::NormsProducer* norms) {
    // Get field info from state
    const auto* fieldInfo = state_.fieldInfos.fieldInfo(fieldName);
    if (!fieldInfo) {
        throw std::runtime_error("Field not found in FieldInfos: " + fieldName);
    }

    // Set field on postings writer
    postingsWriter_->setField(*fieldInfo);

    // Get norms for this field if available
    std::unique_ptr<index::NumericDocValues> normValues;
    if (norms) {
        normValues = norms->getNorms(*fieldInfo);
    }

    // Create BlockTreeTermsWriter for this field
    blocktree::BlockTreeTermsWriter termDictWriter(timOut_.get(), tipOut_.get(), *fieldInfo);

    // Iterate over all terms for this field
    auto termsEnum = terms.iterator();

    int termCount = 0;
    while (termsEnum->next()) {
        auto termBytes = termsEnum->term();
        std::string term(reinterpret_cast<const char*>(termBytes.data()), termBytes.length());

        // Get postings for this term
        auto postingsEnum = termsEnum->postings(false);  // Use one-at-a-time for now

        // Start new term
        postingsWriter_->startTerm();

        // Track term statistics
        int docFreq = 0;
        int64_t totalTermFreq = 0;

        // Iterate over all documents for this term
        int doc;
        while ((doc = postingsEnum->nextDoc()) != index::PostingsEnum::NO_MORE_DOCS) {
            int freq = postingsEnum->freq();

            // Look up norm for this document
            int8_t norm = 0;
            if (normValues && normValues->advanceExact(doc)) {
                norm = static_cast<int8_t>(normValues->longValue() & 0xFF);
            }

            // Write document to postings writer with norm
            postingsWriter_->startDoc(doc, freq, norm);

            // Update statistics
            docFreq++;
            totalTermFreq += freq;
        }

        // Finish term and get term state (file pointers, metadata)
        TermState termState = postingsWriter_->finishTerm();

        // Add term to term dictionary with statistics (including skipStartFP for WAND)
        util::BytesRef termBytesRef(reinterpret_cast<const uint8_t*>(term.data()), term.size());
        blocktree::BlockTreeTermsWriter::TermStats termStats(
            docFreq, totalTermFreq, termState.docStartFP, termState.skipStartFP);

        termDictWriter.addTerm(termBytesRef, termStats);
        termCount++;
    }

    // Set document count before finish (from Terms)
    int docCount = terms.getDocCount();
    termDictWriter.setDocCount(docCount);

    // Finish writing term dictionary for this field
    termDictWriter.finish();

    // Store field-level statistics for metadata file
    FieldMetadata metadata;
    metadata.numTerms = termDictWriter.getNumTerms();
    metadata.sumTotalTermFreq = termDictWriter.getSumTotalTermFreq();
    metadata.sumDocFreq = termDictWriter.getSumDocFreq();
    metadata.docCount = termDictWriter.getDocCount();
    fieldMetadata_[fieldName] = metadata;
}

void Lucene104FieldsConsumer::close() {
    if (closed_) {
        return;
    }

    try {
        // Close term dictionary outputs
        if (timOut_) {
            timOut_->close();
            timOut_.reset();
        }
        if (tipOut_) {
            tipOut_->close();
            tipOut_.reset();
        }

        // Write field metadata (.tmd file)
        std::string tmdFile = state_.segmentName + ".tmd";
        auto tmdOut = state_.directory->createOutput(tmdFile, store::IOContext::DEFAULT);

        // Write number of fields
        tmdOut->writeVInt(static_cast<int>(fieldMetadata_.size()));

        // Write each field's metadata
        for (const auto& entry : fieldMetadata_) {
            const std::string& fieldName = entry.first;
            const FieldMetadata& metadata = entry.second;

            // Write field name
            tmdOut->writeString(fieldName);

            // Write statistics
            tmdOut->writeVLong(metadata.numTerms);
            tmdOut->writeVLong(metadata.sumTotalTermFreq);
            tmdOut->writeVLong(metadata.sumDocFreq);
            tmdOut->writeVInt(metadata.docCount);
        }

        tmdOut->close();
        files_.push_back(tmdFile);

        // Close postings writer and write to disk
        if (postingsWriter_) {
            // Get accumulated bytes from in-memory buffers
            auto docBytes = postingsWriter_->getBytes();
            auto skipBytes = postingsWriter_->getSkipBytes();

            // Close the writer (clears in-memory buffers)
            postingsWriter_->close();

            // Build .doc filename
            std::string docFile = state_.segmentName;
            if (!state_.segmentSuffix.empty()) {
                docFile += "_" + state_.segmentSuffix;
            }
            docFile += ".doc";

            // Write bytes to actual .doc file
            auto docOut = state_.directory->createOutput(docFile, store::IOContext::DEFAULT);
            docOut->writeBytes(docBytes.data(), docBytes.size());
            docOut->close();
            files_.push_back(docFile);

            // Write skip file if we have skip data
            if (!skipBytes.empty()) {
                std::string skipFile = postingsWriter_->getSkipFileName();

                // Write bytes to actual .skp file
                auto skipOut = state_.directory->createOutput(skipFile, store::IOContext::DEFAULT);
                skipOut->writeBytes(skipBytes.data(), skipBytes.size());
                skipOut->close();
                files_.push_back(skipFile);
            }
        }

        closed_ = true;
    } catch (const std::exception& e) {
        throw IOException("Failed to close FieldsConsumer: " + std::string(e.what()));
    }
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
