// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DocumentsWriterPerThread.h"

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/SegmentState.h"
#include "diagon/codecs/SimpleFieldsConsumer.h"
#include "diagon/index/DocValues.h"
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
    , termsWriter_(fieldInfosBuilder_)
    , codecName_("Lucene104") {}

DocumentsWriterPerThread::DocumentsWriterPerThread(const Config& config,
                                                   store::Directory* directory,
                                                   const std::string& codecName)
    : config_(config)
    , termsWriter_(fieldInfosBuilder_)
    , directory_(directory)
    , codecName_(codecName) {}

bool DocumentsWriterPerThread::addDocument(const document::Document& doc) {
    // Add document to terms writer
    termsWriter_.addDocument(doc, nextDocID_);

    // Process stored fields
    bool hasStoredFields = false;
    for (const auto& field : doc.getFields()) {
        const auto& fieldType = field->fieldType();

        // Check if this is a stored field
        if (fieldType.stored) {
            hasStoredFields = true;
            break;
        }
    }

    if (hasStoredFields && directory_) {
        // Lazy initialize stored fields writer on first use
        if (!storedFieldsWriter_) {
            storedFieldsWriter_ = std::make_unique<codecs::StoredFieldsWriter>("_temp");
        }

        // Start document
        storedFieldsWriter_->startDocument();

        // Write stored fields
        for (const auto& field : doc.getFields()) {
            const auto& fieldType = field->fieldType();

            if (fieldType.stored) {
                // Get or create field info
                fieldInfosBuilder_.getOrAdd(field->name());
                FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());

                if (fieldInfo) {
                    // Check field value type and write
                    // Check numeric first, since numeric fields also have string representation
                    if (auto numericVal = field->numericValue()) {
                        storedFieldsWriter_->writeField(*fieldInfo, *numericVal);
                    } else if (auto stringVal = field->stringValue()) {
                        storedFieldsWriter_->writeField(*fieldInfo, *stringVal);
                    }
                }
            }
        }

        // Finish document
        storedFieldsWriter_->finishDocument();
    }

    // Process numeric doc values fields
    for (const auto& field : doc.getFields()) {
        const auto& fieldType = field->fieldType();

        // Check if this is a numeric doc values field
        if (fieldType.docValuesType == DocValuesType::NUMERIC) {
            auto numericValue = field->numericValue();
            if (numericValue) {
                // Lazy initialize doc values writer on first use
                if (!docValuesWriter_ && directory_) {
                    // Create temporary segment name for sizing (actual name created in flush)
                    docValuesWriter_ = std::make_unique<codecs::NumericDocValuesWriter>(
                        "_temp", config_.maxBufferedDocs);
                }

                if (docValuesWriter_) {
                    // Get or create field number, then get FieldInfo
                    fieldInfosBuilder_.getOrAdd(field->name());
                    fieldInfosBuilder_.updateDocValuesType(field->name(), DocValuesType::NUMERIC);
                    FieldInfo* fieldInfo = fieldInfosBuilder_.getFieldInfo(field->name());

                    if (fieldInfo) {
                        // Add value to doc values writer
                        docValuesWriter_->addValue(*fieldInfo, nextDocID_, *numericValue);
                    }
                }
            }
        }
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

    // Add RAM from doc values writer
    if (docValuesWriter_) {
        bytes += docValuesWriter_->ramBytesUsed();
    }

    // Add RAM from stored fields writer
    if (storedFieldsWriter_) {
        bytes += storedFieldsWriter_->ramBytesUsed();
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

    // Get all terms from terms writer
    std::vector<std::string> terms = termsWriter_.getTerms();

    // Sum frequencies for each document
    for (const auto& term : terms) {
        std::vector<int> postings = termsWriter_.getPostingList(term);

        // Posting list format: [docID, freq, docID, freq, ...]
        for (size_t i = 0; i < postings.size(); i += 2) {
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

        // Create codec consumer
        codecs::SimpleFieldsConsumer consumer(state);

        // Get all terms from terms writer
        std::vector<std::string> terms = termsWriter_.getTerms();

        // Build term → posting list map
        std::unordered_map<std::string, std::vector<int>> termPostings;
        for (const auto& term : terms) {
            termPostings[term] = termsWriter_.getPostingList(term);
        }

        // Write posting lists
        consumer.writeField("_all", termPostings);

        // Close consumer
        consumer.close();

        // Add files to SegmentInfo
        for (const auto& file : consumer.getFiles()) {
            segmentInfo->addFile(file);
        }

        // Write stored fields if present
        if (storedFieldsWriter_) {
            // Finish writing
            storedFieldsWriter_->finish(numDocsInRAM_);

            // Create output files for stored fields
            auto dataOut =
                directory_->createOutput(segmentName + ".fdt", store::IOContext::DEFAULT);
            auto indexOut =
                directory_->createOutput(segmentName + ".fdx", store::IOContext::DEFAULT);

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
            auto dataOut =
                directory_->createOutput(segmentName + ".dvd", store::IOContext::DEFAULT);
            auto metaOut =
                directory_->createOutput(segmentName + ".dvm", store::IOContext::DEFAULT);

            // Flush doc values
            docValuesWriter_->flush(*dataOut, *metaOut);

            // Close outputs
            dataOut->close();
            metaOut->close();

            // Add doc values files to SegmentInfo
            segmentInfo->addFile(segmentName + ".dvd");
            segmentInfo->addFile(segmentName + ".dvm");
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

            // Create segment write state for norms (codecs::SegmentWriteState)
            store::IOContext normsContext = store::IOContext::DEFAULT;
            codecs::SegmentWriteState normsState(*directory_, segmentName, "", normsContext);
            normsState.segmentInfo = segmentInfo.get();

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

    // Clear doc values writer
    docValuesWriter_.reset();

    // Clear stored fields writer
    storedFieldsWriter_.reset();

    // Clear field infos
    fieldInfosBuilder_.reset();

    // Reset counters
    numDocsInRAM_ = 0;
    nextDocID_ = 0;
}

}  // namespace index
}  // namespace diagon
