// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DocumentsWriterPerThread.h"

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/PointValuesWriter.h"
#include "diagon/codecs/SegmentState.h"
#include "diagon/codecs/SimpleFieldsConsumer.h"
#include "diagon/codecs/lucene104/Lucene104FieldsConsumer.h"
#include "diagon/index/DocValues.h"
#include "diagon/index/FreqProxFields.h"
#include "diagon/index/InMemoryNormsProducer.h"
#include "diagon/index/SegmentWriteState.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <sstream>

namespace diagon {
namespace index {

namespace {

// ==================== In-Memory Norms Support ====================

/**
 * In-memory NumericDocValues for norms
 * Holds pre-computed norm values for all documents in RAM
 */
class MemoryNumericDocValues : public index::NumericDocValues {
public:
    explicit MemoryNumericDocValues(std::vector<int64_t> values)
        : values_(std::move(values))
        , currentDoc_(-1) {}

    int nextDoc() override {
        currentDoc_++;
        if (currentDoc_ >= static_cast<int>(values_.size())) {
            currentDoc_ = NO_MORE_DOCS;
            return NO_MORE_DOCS;
        }
        return currentDoc_;
    }

    int advance(int target) override {
        if (target >= static_cast<int>(values_.size())) {
            currentDoc_ = NO_MORE_DOCS;
            return NO_MORE_DOCS;
        }
        currentDoc_ = target;
        return currentDoc_;
    }

    bool advanceExact(int target) override {
        if (target < 0 || target >= static_cast<int>(values_.size())) {
            return false;
        }
        currentDoc_ = target;
        return true;
    }

    int64_t cost() const override { return values_.size(); }

    int docID() const override { return currentDoc_; }

    int64_t longValue() const override {
        if (currentDoc_ < 0 || currentDoc_ >= static_cast<int>(values_.size())) {
            return 0;
        }
        return values_[currentDoc_];
    }

    const std::vector<int64_t>& getValues() const { return values_; }

private:
    std::vector<int64_t> values_;
    int currentDoc_;
};

/**
 * In-memory NormsProducer
 * Wraps pre-computed norms for writing to disk
 */
class MemoryNormsProducer : public codecs::NormsProducer {
public:
    explicit MemoryNormsProducer(std::vector<int64_t> norms)
        : norms_(std::move(norms)) {}

    std::unique_ptr<index::NumericDocValues> getNorms(const index::FieldInfo& /*field*/) override {
        // Return a new iterator over the same data
        return std::make_unique<MemoryNumericDocValues>(norms_);
    }

    void checkIntegrity() override {
        // No-op for in-memory norms
    }

    void close() override {
        // No-op for in-memory norms
    }

private:
    std::vector<int64_t> norms_;
};

}  // anonymous namespace

// Static segment counter (atomic for thread safety)
std::atomic<int> DocumentsWriterPerThread::nextSegmentNumber_{0};

DocumentsWriterPerThread::DocumentsWriterPerThread()
    : config_(Config{})
    , termsWriter_(fieldInfosBuilder_, 50000)  // Pre-size for typical corpus (30k-50k terms)
    , codecName_("Lucene104") {}

DocumentsWriterPerThread::DocumentsWriterPerThread(const Config& config,
                                                   store::Directory* directory,
                                                   const std::string& codecName)
    : config_(config)
    , termsWriter_(fieldInfosBuilder_, 50000)  // Pre-size for typical corpus
    , directory_(directory)
    , codecName_(codecName) {}

bool DocumentsWriterPerThread::addDocument(const document::Document& doc) {
    // Single-pass field processing: handle indexing, stored fields, and doc values
    // in one iteration instead of 4 separate passes (4x fewer field iterations)
    bool hasStoredFields = false;

    for (const auto& field : doc.getFields()) {
        const auto& fieldType = field->fieldType();

        // Pass 1: Index terms (delegates to FreqProxTermsWriter::addField)
        if (fieldType.indexOptions != IndexOptions::NONE) {
            termsWriter_.addField(*field, nextDocID_);
        }

        // Pass 2: Stored fields (collected inline)
        if (fieldType.stored) {
            if (!hasStoredFields && directory_) {
                // Lazy initialize stored fields writer on first stored field
                if (!storedFieldsWriter_) {
                    storedFieldsWriter_ = std::make_unique<codecs::StoredFieldsWriter>("_temp");
                }
                storedFieldsWriter_->startDocument();
                hasStoredFields = true;
            }

            if (hasStoredFields) {
                fieldInfosBuilder_.getOrAdd(field->name());

                // Propagate numeric type to field metadata if present
                if (fieldType.numericType != document::NumericType::NONE) {
                    const char* numericTypeStr = nullptr;
                    switch (fieldType.numericType) {
                        case document::NumericType::LONG:
                            numericTypeStr = "LONG";
                            break;
                        case document::NumericType::DOUBLE:
                            numericTypeStr = "DOUBLE";
                            break;
                        case document::NumericType::INT:
                            numericTypeStr = "INT";
                            break;
                        case document::NumericType::FLOAT:
                            numericTypeStr = "FLOAT";
                            break;
                        default:
                            break;
                    }
                    if (numericTypeStr) {
                        fieldInfosBuilder_.setAttribute(field->name(), "numeric_type",
                                                        numericTypeStr);
                    }
                }

                FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                if (fieldInfo) {
                    if (auto numericVal = field->numericValue()) {
                        storedFieldsWriter_->writeField(*fieldInfo, *numericVal);
                    } else if (auto stringVal = field->stringValue()) {
                        storedFieldsWriter_->writeField(*fieldInfo, *stringVal);
                    }
                }
            }
        }

        // Pass 3: Numeric doc values (collected inline)
        if (fieldType.docValuesType == DocValuesType::NUMERIC) {
            auto numericValue = field->numericValue();
            if (numericValue) {
                // Always register the field in metadata, even without a directory
                fieldInfosBuilder_.getOrAdd(field->name());
                fieldInfosBuilder_.updateDocValuesType(field->name(), DocValuesType::NUMERIC);

                if (fieldType.numericType != document::NumericType::NONE) {
                    const char* numericTypeStr = nullptr;
                    switch (fieldType.numericType) {
                        case document::NumericType::LONG:
                            numericTypeStr = "LONG";
                            break;
                        case document::NumericType::DOUBLE:
                            numericTypeStr = "DOUBLE";
                            break;
                        case document::NumericType::INT:
                            numericTypeStr = "INT";
                            break;
                        case document::NumericType::FLOAT:
                            numericTypeStr = "FLOAT";
                            break;
                        default:
                            break;
                    }
                    if (numericTypeStr) {
                        fieldInfosBuilder_.setAttribute(field->name(), "numeric_type",
                                                        numericTypeStr);
                    }
                }

                // Write doc values data if directory is available
                if (!docValuesWriter_ && directory_) {
                    docValuesWriter_ = std::make_unique<codecs::NumericDocValuesWriter>(
                        "_temp", config_.maxBufferedDocs);
                }

                if (docValuesWriter_) {
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                    if (fieldInfo) {
                        docValuesWriter_->addValue(*fieldInfo, nextDocID_, *numericValue);
                    }
                }
            }
        }

        // Pass 3b: Sorted doc values
        if (fieldType.docValuesType == DocValuesType::SORTED) {
            auto stringVal = field->stringValue();
            if (stringVal) {
                fieldInfosBuilder_.getOrAdd(field->name());
                fieldInfosBuilder_.updateDocValuesType(field->name(), DocValuesType::SORTED);

                if (!sortedDocValuesWriter_ && directory_) {
                    sortedDocValuesWriter_ = std::make_unique<codecs::SortedDocValuesWriter>(
                        "_temp", config_.maxBufferedDocs);
                }

                if (sortedDocValuesWriter_) {
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                    if (fieldInfo) {
                        sortedDocValuesWriter_->addValue(*fieldInfo, nextDocID_, *stringVal);
                    }
                }
            }
        }

        // Pass 3c: Binary doc values
        if (fieldType.docValuesType == DocValuesType::BINARY) {
            auto stringVal = field->stringValue();
            auto binaryVal = field->binaryValue();
            if (stringVal || binaryVal) {
                fieldInfosBuilder_.getOrAdd(field->name());
                fieldInfosBuilder_.updateDocValuesType(field->name(), DocValuesType::BINARY);

                if (!binaryDocValuesWriter_ && directory_) {
                    binaryDocValuesWriter_ = std::make_unique<codecs::BinaryDocValuesWriter>(
                        "_temp", config_.maxBufferedDocs);
                }

                if (binaryDocValuesWriter_) {
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                    if (fieldInfo) {
                        if (binaryVal) {
                            binaryDocValuesWriter_->addValue(*fieldInfo, nextDocID_,
                                                             binaryVal->data(),
                                                             static_cast<int>(binaryVal->size()));
                        } else if (stringVal) {
                            binaryDocValuesWriter_->addValue(*fieldInfo, nextDocID_, *stringVal);
                        }
                    }
                }
            }
        }

        // Pass 3d: Sorted numeric doc values
        if (fieldType.docValuesType == DocValuesType::SORTED_NUMERIC) {
            auto numericValue = field->numericValue();
            if (numericValue) {
                fieldInfosBuilder_.getOrAdd(field->name());
                fieldInfosBuilder_.updateDocValuesType(field->name(),
                                                       DocValuesType::SORTED_NUMERIC);

                if (!sortedNumericDocValuesWriter_ && directory_) {
                    sortedNumericDocValuesWriter_ =
                        std::make_unique<codecs::SortedNumericDocValuesWriter>(
                            "_temp", config_.maxBufferedDocs);
                }

                if (sortedNumericDocValuesWriter_) {
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                    if (fieldInfo) {
                        sortedNumericDocValuesWriter_->addValue(*fieldInfo, nextDocID_,
                                                                *numericValue);
                    }
                }
            }
        }

        // Pass 3e: Sorted set doc values
        if (fieldType.docValuesType == DocValuesType::SORTED_SET) {
            auto stringVal = field->stringValue();
            if (stringVal) {
                fieldInfosBuilder_.getOrAdd(field->name());
                fieldInfosBuilder_.updateDocValuesType(field->name(), DocValuesType::SORTED_SET);

                if (!sortedSetDocValuesWriter_ && directory_) {
                    sortedSetDocValuesWriter_ = std::make_unique<codecs::SortedSetDocValuesWriter>(
                        "_temp", config_.maxBufferedDocs);
                }

                if (sortedSetDocValuesWriter_) {
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                    if (fieldInfo) {
                        sortedSetDocValuesWriter_->addValue(*fieldInfo, nextDocID_, *stringVal);
                    }
                }
            }
        }

        // Pass 4: Point values (BKD tree)
        if (fieldType.hasPointValues()) {
            auto binaryVal = field->binaryValue();
            if (binaryVal) {
                // Register field with point dimensions
                fieldInfosBuilder_.getOrAdd(field->name());
                fieldInfosBuilder_.updatePointDimensions(
                    field->name(), fieldType.pointDimensionCount,
                    fieldType.pointIndexDimensionCount, fieldType.pointNumBytes);

                // Lazy initialize point values writer
                if (!pointValuesWriter_ && directory_) {
                    pointValuesWriter_ = std::make_unique<codecs::PointValuesWriter>(
                        "_temp", config_.maxBufferedDocs);
                }

                if (pointValuesWriter_) {
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());
                    if (fieldInfo) {
                        pointValuesWriter_->addPoint(*fieldInfo, nextDocID_, binaryVal->data());
                    }
                }
            }
        }
    }

    // Finalize stored fields for this document
    if (hasStoredFields) {
        storedFieldsWriter_->finishDocument();
    }

    // Increment counters
    numDocsInRAM_++;
    nextDocID_++;

    // Check if flush needed
    return needsFlush();
}

int64_t DocumentsWriterPerThread::bytesUsed() const {
    // Get RAM from terms writer
    int64_t bytes = termsWriter_.bytesUsed();

    // Add RAM from doc values writers
    if (docValuesWriter_) {
        bytes += docValuesWriter_->ramBytesUsed();
    }
    if (sortedDocValuesWriter_) {
        bytes += sortedDocValuesWriter_->ramBytesUsed();
    }
    if (binaryDocValuesWriter_) {
        bytes += binaryDocValuesWriter_->ramBytesUsed();
    }
    if (sortedNumericDocValuesWriter_) {
        bytes += sortedNumericDocValuesWriter_->ramBytesUsed();
    }
    if (sortedSetDocValuesWriter_) {
        bytes += sortedSetDocValuesWriter_->ramBytesUsed();
    }

    // Add RAM from stored fields writer
    if (storedFieldsWriter_) {
        bytes += storedFieldsWriter_->ramBytesUsed();
    }

    // Add RAM from point values writer
    if (pointValuesWriter_) {
        bytes += pointValuesWriter_->ramBytesUsed();
    }

    // Add field metadata overhead (approximate)
    bytes += fieldInfosBuilder_.getFieldCount() * 256;

    // Add document storage overhead (approximate)
    bytes += numDocsInRAM_ * 64;

    return bytes;
}

bool DocumentsWriterPerThread::needsFlush() const {
    // Check document count limit
    if (numDocsInRAM_ >= config_.maxBufferedDocs) {
        return true;
    }

    // Check RAM limit (convert MB to bytes)
    int64_t ramLimitBytes = config_.ramBufferSizeMB * 1024 * 1024;
    if (bytesUsed() >= ramLimitBytes) {
        return true;
    }

    return false;
}

/**
 * Compute norms for a field from posting lists
 *
 * Norms encode document field length for BM25 scoring.
 * This method sums up term frequencies for each document.
 *
 * @param fieldName Field to compute norms for
 * @param numDocs Number of documents in segment
 * @return Vector of encoded norm values (1 byte per document)
 */
std::vector<int64_t> DocumentsWriterPerThread::computeNorms(const std::string& fieldName,
                                                            int numDocs) {
    // Initialize norm values (field length per document)
    std::vector<int64_t> fieldLengths(numDocs, 0);

    // Get terms for THIS field only (O(n) instead of O(n²))
    std::vector<std::string> terms = termsWriter_.getTermsForField(fieldName);

    // Sum frequencies for each document
    for (const auto& term : terms) {
        // Use field-specific O(1) lookup instead of O(n) linear scan
        std::vector<int> postings = termsWriter_.getPostingList(fieldName, term);

        // Posting list format: [docID, freq, docID, freq, ...]
        for (size_t i = 0; i + 1 < postings.size(); i += 2) {
            int docID = postings[i];
            int freq = postings[i + 1];

            if (docID >= 0 && docID < numDocs) {
                fieldLengths[docID] += freq;
            }
        }
    }

    // Encode field lengths to norm bytes using simplified SmallFloat encoding
    // Norm = min(127, 127 / sqrt(length))
    // Shorter documents get higher norms (more weight in scoring)
    std::vector<int64_t> norms(numDocs);
    for (int doc = 0; doc < numDocs; doc++) {
        int64_t length = fieldLengths[doc];

        if (length <= 0) {
            norms[doc] = 127;  // Empty field gets maximum norm
        } else {
            // Calculate: 127 / sqrt(length)
            double sqrtLength = std::sqrt(static_cast<double>(length));
            double encoded = 127.0 / sqrtLength;

            // Clamp to signed byte range and store as int64
            if (encoded > 127.0) {
                norms[doc] = 127;
            } else if (encoded < -128.0) {
                norms[doc] = -128;
            } else {
                norms[doc] = static_cast<int64_t>(encoded);
            }
        }
    }

    return norms;
}

std::shared_ptr<SegmentInfo> DocumentsWriterPerThread::flush() {
    // Validate we have documents
    if (numDocsInRAM_ == 0) {
        return nullptr;  // Nothing to flush
    }

    // Generate unique segment name with timestamp + counter for uniqueness across tests
    auto now = std::chrono::system_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    int counter = nextSegmentNumber_.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream oss;
    oss << "_" << std::hex << timestamp << "_" << counter;
    std::string segmentName = oss.str();

    // Create SegmentInfo
    auto segmentInfo = std::make_shared<SegmentInfo>(segmentName, numDocsInRAM_, codecName_);

    // If directory available, write posting lists to disk
    if (directory_) {
        // Phase 3: Add "_all" field to FieldInfosBuilder (all fields combined into _all)
        fieldInfosBuilder_.getOrAdd("_all");
        fieldInfosBuilder_.updateIndexOptions("_all", IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);

        // Build FieldInfos from builder
        auto fieldInfosPtr = fieldInfosBuilder_.finish();
        if (!fieldInfosPtr) {
            // No field infos - skip flush
            reset();
            return segmentInfo;
        }

        // Set FieldInfos on SegmentInfo (Phase 4) - move ownership
        segmentInfo->setFieldInfos(std::move(*fieldInfosPtr));

        // Create segment write state - reference FieldInfos from SegmentInfo
        SegmentWriteState state(directory_, segmentName, numDocsInRAM_, segmentInfo->fieldInfos(),
                                "");

        // Get codec by name and create fields consumer
        auto& codec = codecs::Codec::forName(segmentInfo->codecName());
        auto consumer = codec.postingsFormat().fieldsConsumer(state);

        if (!consumer) {
            throw std::runtime_error("Codec returned null FieldsConsumer");
        }

        // Create Fields wrapper around in-memory postings (pass FieldInfos to expose actual fields)
        FreqProxFields fields(termsWriter_, segmentInfo->fieldInfos());

        // Create norms producer from field lengths computed during indexing
        auto normsProducer = std::make_unique<InMemoryNormsProducer>();

        // Populate norms from field lengths (flat vector iteration by field ID)
        termsWriter_.forEachFieldLength(
            [&normsProducer](const std::string& fieldName,
                             const FreqProxTermsWriter::FieldLengthData& fieldLengthData) {
                const auto& lengths = fieldLengthData.lengths;
                for (int docID = 0; docID < static_cast<int>(lengths.size()); docID++) {
                    if (lengths[docID] != 0) {
                        normsProducer->setNormFromLength(fieldName, docID, lengths[docID]);
                    }
                }
            });

        // Use streaming API - codec iterates over fields/terms/postings
        consumer->write(fields, normsProducer.get());

        // Close consumer
        consumer->close();

        // Add files to SegmentInfo (hack: cast to get files)
        // TODO Phase 4.1: Make getFiles() part of FieldsConsumer interface
        auto* lucene104Consumer = dynamic_cast<codecs::lucene104::Lucene104FieldsConsumer*>(
            consumer.get());
        auto* simpleConsumer = dynamic_cast<codecs::SimpleFieldsConsumer*>(consumer.get());

        if (lucene104Consumer) {
            for (const auto& file : lucene104Consumer->getFiles()) {
                segmentInfo->addFile(file);
            }
        } else if (simpleConsumer) {
            for (const auto& file : simpleConsumer->getFiles()) {
                segmentInfo->addFile(file);
            }
        } else {
            throw std::runtime_error("Unknown FieldsConsumer type");
        }

        // Write stored fields if present
        if (storedFieldsWriter_) {
            // Finish writing
            storedFieldsWriter_->finish(numDocsInRAM_);

            // Create output files for stored fields
            auto dataOut = directory_->createOutput(segmentName + ".fdt",
                                                    store::IOContext::DEFAULT);
            auto indexOut = directory_->createOutput(segmentName + ".fdx",
                                                     store::IOContext::DEFAULT);

            // Flush stored fields
            storedFieldsWriter_->flush(*dataOut, *indexOut);

            // Close outputs
            dataOut->close();
            indexOut->close();

            // Add stored fields files to SegmentInfo
            segmentInfo->addFile(segmentName + ".fdt");
            segmentInfo->addFile(segmentName + ".fdx");
        }

        // Write doc values if present
        if (docValuesWriter_) {
            // Finish all numeric doc values fields
            for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
                if (fieldInfo.docValuesType == DocValuesType::NUMERIC) {
                    docValuesWriter_->finishField(fieldInfo);
                }
            }

            // Create output files for doc values
            auto dataOut = directory_->createOutput(segmentName + ".dvd",
                                                    store::IOContext::DEFAULT);
            auto metaOut = directory_->createOutput(segmentName + ".dvm",
                                                    store::IOContext::DEFAULT);

            // Flush doc values
            docValuesWriter_->flush(*dataOut, *metaOut);

            // Close outputs
            dataOut->close();
            metaOut->close();

            // Add doc values files to SegmentInfo
            segmentInfo->addFile(segmentName + ".dvd");
            segmentInfo->addFile(segmentName + ".dvm");
        }

        // Write sorted doc values if present
        if (sortedDocValuesWriter_) {
            for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
                if (fieldInfo.docValuesType == DocValuesType::SORTED) {
                    sortedDocValuesWriter_->finishField(fieldInfo);
                }
            }
            auto dataOut = directory_->createOutput(segmentName + ".sdvd",
                                                    store::IOContext::DEFAULT);
            auto metaOut = directory_->createOutput(segmentName + ".sdvm",
                                                    store::IOContext::DEFAULT);
            sortedDocValuesWriter_->flush(*dataOut, *metaOut);
            dataOut->close();
            metaOut->close();
            segmentInfo->addFile(segmentName + ".sdvd");
            segmentInfo->addFile(segmentName + ".sdvm");
        }

        // Write binary doc values if present
        if (binaryDocValuesWriter_) {
            for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
                if (fieldInfo.docValuesType == DocValuesType::BINARY) {
                    binaryDocValuesWriter_->finishField(fieldInfo);
                }
            }
            auto dataOut = directory_->createOutput(segmentName + ".bdvd",
                                                    store::IOContext::DEFAULT);
            auto metaOut = directory_->createOutput(segmentName + ".bdvm",
                                                    store::IOContext::DEFAULT);
            binaryDocValuesWriter_->flush(*dataOut, *metaOut);
            dataOut->close();
            metaOut->close();
            segmentInfo->addFile(segmentName + ".bdvd");
            segmentInfo->addFile(segmentName + ".bdvm");
        }

        // Write sorted numeric doc values if present
        if (sortedNumericDocValuesWriter_) {
            for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
                if (fieldInfo.docValuesType == DocValuesType::SORTED_NUMERIC) {
                    sortedNumericDocValuesWriter_->finishField(fieldInfo);
                }
            }
            auto dataOut = directory_->createOutput(segmentName + ".sndvd",
                                                    store::IOContext::DEFAULT);
            auto metaOut = directory_->createOutput(segmentName + ".sndvm",
                                                    store::IOContext::DEFAULT);
            sortedNumericDocValuesWriter_->flush(*dataOut, *metaOut);
            dataOut->close();
            metaOut->close();
            segmentInfo->addFile(segmentName + ".sndvd");
            segmentInfo->addFile(segmentName + ".sndvm");
        }

        // Write sorted set doc values if present
        if (sortedSetDocValuesWriter_) {
            for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
                if (fieldInfo.docValuesType == DocValuesType::SORTED_SET) {
                    sortedSetDocValuesWriter_->finishField(fieldInfo);
                }
            }
            auto dataOut = directory_->createOutput(segmentName + ".ssvd",
                                                    store::IOContext::DEFAULT);
            auto metaOut = directory_->createOutput(segmentName + ".ssvm",
                                                    store::IOContext::DEFAULT);
            sortedSetDocValuesWriter_->flush(*dataOut, *metaOut);
            dataOut->close();
            metaOut->close();
            segmentInfo->addFile(segmentName + ".ssvd");
            segmentInfo->addFile(segmentName + ".ssvm");
        }

        // Write point values (BKD tree) if present
        if (pointValuesWriter_ && pointValuesWriter_->hasPoints()) {
            auto kdmOut = directory_->createOutput(segmentName + ".kdm", store::IOContext::DEFAULT);
            auto kdiOut = directory_->createOutput(segmentName + ".kdi", store::IOContext::DEFAULT);
            auto kddOut = directory_->createOutput(segmentName + ".kdd", store::IOContext::DEFAULT);

            pointValuesWriter_->flush(*kdmOut, *kdiOut, *kddOut);

            kdmOut->close();
            kdiOut->close();
            kddOut->close();

            segmentInfo->addFile(segmentName + ".kdm");
            segmentInfo->addFile(segmentName + ".kdi");
            segmentInfo->addFile(segmentName + ".kdd");
        }

        // Write norms for indexed fields
        bool hasNorms = false;
        for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
            if (!fieldInfo.omitNorms && fieldInfo.indexOptions != IndexOptions::NONE) {
                hasNorms = true;
                break;
            }
        }

        if (hasNorms) {
            // Get codec and create norms consumer
            auto& codec = codecs::Codec::forName(codecName_);
            auto& normsFormat = codec.normsFormat();

            // Create segment write state for norms
            SegmentWriteState normsState(directory_, segmentName, numDocsInRAM_,
                                         segmentInfo->fieldInfos(), "");

            auto normsConsumer = normsFormat.normsConsumer(normsState);

            // Write norms for each indexed field
            for (const auto& fieldInfo : segmentInfo->fieldInfos()) {
                if (!fieldInfo.omitNorms && fieldInfo.indexOptions != IndexOptions::NONE) {
                    // Compute norms from posting lists
                    std::vector<int64_t> norms = computeNorms(fieldInfo.name, numDocsInRAM_);

                    // Create in-memory norms producer
                    auto normsProducer = MemoryNormsProducer(std::move(norms));

                    // Write norms to disk
                    normsConsumer->addNormsField(fieldInfo, normsProducer);
                }
            }

            // Close norms consumer
            normsConsumer->close();

            // Add norms files to SegmentInfo
            segmentInfo->addFile(segmentName + ".nvd");
            segmentInfo->addFile(segmentName + ".nvm");
        }

        // Set diagnostics
        segmentInfo->setDiagnostic("source", "flush");
        segmentInfo->setDiagnostic("os", "linux");
    }

    // Reset for next segment
    reset();

    return segmentInfo;
}

void DocumentsWriterPerThread::reset() {
    // Clear terms writer
    termsWriter_.reset();

    // Clear doc values writers
    docValuesWriter_.reset();
    sortedDocValuesWriter_.reset();
    binaryDocValuesWriter_.reset();
    sortedNumericDocValuesWriter_.reset();
    sortedSetDocValuesWriter_.reset();

    // Clear stored fields writer
    storedFieldsWriter_.reset();

    // Clear point values writer
    pointValuesWriter_.reset();

    // Clear field infos
    fieldInfosBuilder_.reset();

    // Reset counters
    numDocsInRAM_ = 0;
    nextDocID_ = 0;
}

}  // namespace index
}  // namespace diagon
