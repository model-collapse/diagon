// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104OSPostingsFormat.h"

#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsWriter.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/Fields.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/BytesRef.h"
#include "diagon/util/Exceptions.h"

#include <iostream>
#include <map>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== OS-Compat FieldsConsumer ====================

/**
 * FieldsConsumer using OS-compat postings writer (ForUtil/PForUtil, 256-blocks).
 *
 * Pipeline:
 * - Lucene104OSPostingsWriter writes .doc/.pos in Lucene 104 wire format
 * - BlockTreeTermsWriter writes .tim/.tip term dictionary (Diagon native format)
 * - .tmd stores per-field metadata
 *
 * Singleton optimization is disabled because BlockTreeTermsWriter doesn't
 * store singletonDocID in the term dictionary.
 */
class Lucene104OSFieldsConsumer : public FieldsConsumer {
public:
    explicit Lucene104OSFieldsConsumer(index::SegmentWriteState& state)
        : state_(state) {
        // Create OS-compat postings writer
        postingsWriter_ = std::make_unique<Lucene104OSPostingsWriter>(state);
        // Disable singleton optimization since BlockTreeTermsWriter doesn't store singletonDocID
        postingsWriter_->setDisableSingleton(true);

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

    ~Lucene104OSFieldsConsumer() override {
        if (!closed_) {
            try {
                close();
            } catch (...) {
            }
        }
    }

    const std::vector<std::string>& getFiles() const override { return files_; }

    void write(index::Fields& fields, codecs::NormsProducer* norms) override {
        if (closed_) {
            throw AlreadyClosedException("FieldsConsumer already closed");
        }

        auto fieldIterator = fields.iterator();
        while (fieldIterator->hasNext()) {
            std::string fieldName = fieldIterator->next();
            auto terms = fields.terms(fieldName);
            if (!terms) continue;
            writeField(fieldName, *terms, norms);
        }
    }

    void close() override {
        if (closed_) return;

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
            tmdOut->writeVInt(static_cast<int>(fieldMetadata_.size()));

            for (const auto& entry : fieldMetadata_) {
                tmdOut->writeString(entry.first);
                tmdOut->writeVLong(entry.second.numTerms);
                tmdOut->writeVLong(entry.second.sumTotalTermFreq);
                tmdOut->writeVLong(entry.second.sumDocFreq);
                tmdOut->writeVInt(entry.second.docCount);
            }
            tmdOut->close();
            files_.push_back(tmdFile);

            // Register .doc/.pos/.psm files and close postings writer
            if (postingsWriter_) {
                files_.push_back(postingsWriter_->getDocFileName());
                if (!postingsWriter_->getPosFileName().empty()) {
                    files_.push_back(postingsWriter_->getPosFileName());
                }
                postingsWriter_->close();
                postingsWriter_.reset();
            }

            closed_ = true;
        } catch (const std::exception& e) {
            throw IOException("Failed to close Lucene104OSFieldsConsumer: " + std::string(e.what()));
        }
    }

private:
    struct FieldMetadata {
        int64_t numTerms;
        int64_t sumTotalTermFreq;
        int64_t sumDocFreq;
        int docCount;
    };

    index::SegmentWriteState& state_;
    std::unique_ptr<Lucene104OSPostingsWriter> postingsWriter_;
    std::unique_ptr<store::IndexOutput> timOut_;
    std::unique_ptr<store::IndexOutput> tipOut_;
    std::vector<std::string> files_;
    bool closed_{false};
    std::map<std::string, FieldMetadata> fieldMetadata_;

    void writeField(const std::string& fieldName, index::Terms& terms,
                    codecs::NormsProducer* norms) {
        const auto* fieldInfo = state_.fieldInfos.fieldInfo(fieldName);
        if (!fieldInfo) {
            throw std::runtime_error("Field not found in FieldInfos: " + fieldName);
        }

        postingsWriter_->setField(*fieldInfo);
        bool writePositions = fieldInfo->hasPositions();

        // Create BlockTreeTermsWriter for this field's term dictionary
        blocktree::BlockTreeTermsWriter termDictWriter(timOut_.get(), tipOut_.get(), *fieldInfo);

        auto termsEnum = terms.iterator();
        while (termsEnum->next()) {
            auto termBytes = termsEnum->term();
            std::string term(reinterpret_cast<const char*>(termBytes.data()), termBytes.length());

            auto postingsEnum = termsEnum->postings(false);

            postingsWriter_->startTerm();

            int docFreq = 0;
            int64_t totalTermFreq = 0;

            int doc;
            while ((doc = postingsEnum->nextDoc()) != index::PostingsEnum::NO_MORE_DOCS) {
                int freq = postingsEnum->freq();
                postingsWriter_->startDoc(doc, freq);

                if (writePositions) {
                    for (int p = 0; p < freq; p++) {
                        int pos = postingsEnum->nextPosition();
                        postingsWriter_->addPosition(pos);
                    }
                }

                postingsWriter_->finishDoc();

                docFreq++;
                totalTermFreq += freq;
            }

            OSTermState termState = postingsWriter_->finishTerm();

            // Map OSTermState to BlockTreeTermsWriter::TermStats
            util::BytesRef termBytesRef(reinterpret_cast<const uint8_t*>(term.data()), term.size());
            blocktree::BlockTreeTermsWriter::TermStats termStats(
                docFreq, totalTermFreq, termState.docStartFP,
                -1,  // skipStartFP: skip data embedded in .doc file
                termState.posStartFP);

            termDictWriter.addTerm(termBytesRef, termStats);
        }

        int docCount = terms.getDocCount();
        termDictWriter.setDocCount(docCount);
        termDictWriter.finish();

        FieldMetadata metadata;
        metadata.numTerms = termDictWriter.getNumTerms();
        metadata.sumTotalTermFreq = termDictWriter.getSumTotalTermFreq();
        metadata.sumDocFreq = termDictWriter.getSumDocFreq();
        metadata.docCount = termDictWriter.getDocCount();
        fieldMetadata_[fieldName] = metadata;
    }
};

// ==================== OS-Compat FieldsProducer ====================

/**
 * Terms wrapper that wires OS-compat PostingsReader to TermsEnum.
 */
class OSBlockTreeTerms : public index::Terms {
public:
    OSBlockTreeTerms(std::shared_ptr<blocktree::BlockTreeTermsReader> reader,
                     Lucene104OSPostingsReader* postingsReader, const index::FieldInfo* fieldInfo,
                     int64_t sumTotalTermFreq, int64_t sumDocFreq, int docCount)
        : reader_(reader)
        , postingsReader_(postingsReader)
        , fieldInfo_(fieldInfo)
        , sumTotalTermFreq_(sumTotalTermFreq)
        , sumDocFreq_(sumDocFreq)
        , docCount_(docCount) {}

    std::unique_ptr<index::TermsEnum> iterator() const override {
        auto termsEnum = reader_->iterator();
        auto* segmentEnum = dynamic_cast<blocktree::SegmentTermsEnum*>(termsEnum.get());
        if (segmentEnum && postingsReader_ && fieldInfo_) {
            // Pass as PostingsReaderBase* so SegmentTermsEnum can cast correctly
            segmentEnum->setPostingsReader(
                static_cast<codecs::PostingsReaderBase*>(postingsReader_), fieldInfo_);
        }
        return termsEnum;
    }

    int64_t size() const override { return reader_->getNumTerms(); }
    int getDocCount() const override { return docCount_; }
    int64_t getSumTotalTermFreq() const override { return sumTotalTermFreq_; }
    int64_t getSumDocFreq() const override { return sumDocFreq_; }

private:
    std::shared_ptr<blocktree::BlockTreeTermsReader> reader_;
    Lucene104OSPostingsReader* postingsReader_;
    const index::FieldInfo* fieldInfo_;
    int64_t sumTotalTermFreq_;
    int64_t sumDocFreq_;
    int docCount_;
};

/**
 * FieldsProducer using OS-compat postings reader.
 *
 * Reads .tim/.tip (Diagon native term dict) + .doc/.pos (Lucene 104 OS-compat).
 */
class Lucene104OSFieldsProducer : public FieldsProducer {
public:
    explicit Lucene104OSFieldsProducer(index::SegmentReadState& state)
        : segmentName_(state.segmentName)
        , fieldInfos_(state.fieldInfos) {
        // Open .tim and .tip files
        timInput_ = state.directory->openInput(segmentName_ + ".tim", store::IOContext::READ);
        tipInput_ = state.directory->openInput(segmentName_ + ".tip", store::IOContext::READ);

        // Read field metadata from .tmd
        try {
            auto tmdInput = state.directory->openInput(segmentName_ + ".tmd", store::IOContext::READ);
            int numFields = tmdInput->readVInt();
            for (int i = 0; i < numFields; i++) {
                std::string fieldName = tmdInput->readString();
                FieldMetadata metadata;
                metadata.numTerms = tmdInput->readVLong();
                metadata.sumTotalTermFreq = tmdInput->readVLong();
                metadata.sumDocFreq = tmdInput->readVLong();
                metadata.docCount = tmdInput->readVInt();
                fieldMetadata_[fieldName] = metadata;
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not read field metadata: " << e.what() << std::endl;
        }

        // Create OS-compat postings reader
        postingsReader_ = std::make_unique<Lucene104OSPostingsReader>(state);

        // Open .pos file if it exists
        try {
            auto posInput = state.directory->openInput(segmentName_ + ".pos", store::IOContext::READ);
            postingsReader_->setPosInput(std::move(posInput));
        } catch (const std::exception&) {
            // No position data — that's OK
        }
    }

    ~Lucene104OSFieldsProducer() override { close(); }

    std::unique_ptr<index::Terms> terms(const std::string& field) override {
        const index::FieldInfo* fieldInfo = fieldInfos_.fieldInfo(field);
        if (!fieldInfo || !fieldInfo->hasPostings()) return nullptr;

        int64_t sumTotalTermFreq = -1;
        int64_t sumDocFreq = -1;
        int docCount = 0;
        auto metaIt = fieldMetadata_.find(field);
        if (metaIt != fieldMetadata_.end()) {
            sumTotalTermFreq = metaIt->second.sumTotalTermFreq;
            sumDocFreq = metaIt->second.sumDocFreq;
            docCount = metaIt->second.docCount;
        }

        auto it = fieldReaders_.find(field);
        if (it != fieldReaders_.end()) {
            return std::make_unique<OSBlockTreeTerms>(
                it->second.reader, postingsReader_.get(), fieldInfo,
                sumTotalTermFreq, sumDocFreq, docCount);
        }

        auto timClone = timInput_->clone();
        auto tipClone = tipInput_->clone();

        std::shared_ptr<blocktree::BlockTreeTermsReader> reader;
        try {
            reader = std::make_shared<blocktree::BlockTreeTermsReader>(
                timClone.get(), tipClone.get(), *fieldInfo);
        } catch (const std::exception&) {
            return nullptr;
        }

        FieldReaderHolder holder;
        holder.reader = reader;
        holder.timInputClone = std::move(timClone);
        holder.tipInputClone = std::move(tipClone);
        fieldReaders_[field] = std::move(holder);

        return std::make_unique<OSBlockTreeTerms>(
            reader, postingsReader_.get(), fieldInfo,
            sumTotalTermFreq, sumDocFreq, docCount);
    }

    void checkIntegrity() override {}

    void close() override {
        fieldReaders_.clear();
        if (postingsReader_) {
            postingsReader_->close();
            postingsReader_.reset();
        }
        timInput_.reset();
        tipInput_.reset();
    }

private:
    struct FieldMetadata {
        int64_t numTerms;
        int64_t sumTotalTermFreq;
        int64_t sumDocFreq;
        int docCount;
    };

    struct FieldReaderHolder {
        std::shared_ptr<blocktree::BlockTreeTermsReader> reader;
        std::unique_ptr<store::IndexInput> timInputClone;
        std::unique_ptr<store::IndexInput> tipInputClone;
    };

    std::string segmentName_;
    const index::FieldInfos& fieldInfos_;
    std::map<std::string, FieldMetadata> fieldMetadata_;
    std::unique_ptr<store::IndexInput> timInput_;
    std::unique_ptr<store::IndexInput> tipInput_;
    std::unique_ptr<Lucene104OSPostingsReader> postingsReader_;
    mutable std::unordered_map<std::string, FieldReaderHolder> fieldReaders_;
};

// ==================== Lucene104OSPostingsFormat ====================

std::unique_ptr<FieldsConsumer> Lucene104OSPostingsFormat::fieldsConsumer(
    index::SegmentWriteState& state) {
    return std::make_unique<Lucene104OSFieldsConsumer>(state);
}

std::unique_ptr<FieldsProducer> Lucene104OSPostingsFormat::fieldsProducer(
    index::SegmentReadState& state) {
    return std::make_unique<Lucene104OSFieldsProducer>(state);
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
