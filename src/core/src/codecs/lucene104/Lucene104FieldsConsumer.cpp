// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104FieldsConsumer.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/index/PostingsEnum.h"
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

void Lucene104FieldsConsumer::write(index::Fields& fields, index::NormsProducer* norms) {
    if (closed_) {
        throw AlreadyClosedException("FieldsConsumer already closed");
    }

    // Note: norms parameter not used yet (Phase 4.2: norms integration)
    (void)norms;

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


        // Write this field
        writeField(fieldName, *terms);

    }

}

void Lucene104FieldsConsumer::writeField(const std::string& fieldName, index::Terms& terms) {
    // Get field info from state
    const auto* fieldInfo = state_.fieldInfos.fieldInfo(fieldName);
    if (!fieldInfo) {
        throw std::runtime_error("Field not found in FieldInfos: " + fieldName);
    }

    // Set field on postings writer
    postingsWriter_->setField(*fieldInfo);

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

            // Write document to postings writer
            postingsWriter_->startDoc(doc, freq);

            // Update statistics
            docFreq++;
            totalTermFreq += freq;
        }

        // Finish term and get term state (file pointers, metadata)
        TermState termState = postingsWriter_->finishTerm();

        // Add term to term dictionary with statistics
        util::BytesRef termBytesRef(reinterpret_cast<const uint8_t*>(term.data()), term.size());
        blocktree::BlockTreeTermsWriter::TermStats termStats(
            docFreq, totalTermFreq, termState.docStartFP);

        termDictWriter.addTerm(termBytesRef, termStats);
        termCount++;
    }


    // Finish writing term dictionary for this field
    termDictWriter.finish();
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

        // Close postings writer and write to disk
        if (postingsWriter_) {
            // Get accumulated bytes from in-memory buffer
            auto docBytes = postingsWriter_->getBytes();

            // Close the writer (clears in-memory buffer)
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
        }

        closed_ = true;
    } catch (const std::exception& e) {
        throw IOException("Failed to close FieldsConsumer: " + std::string(e.what()));
    }
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
