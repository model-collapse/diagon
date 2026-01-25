// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DocumentsWriterPerThread.h"

#include "diagon/codecs/SimpleFieldsConsumer.h"
#include "diagon/index/SegmentWriteState.h"

#include <atomic>
#include <chrono>
#include <sstream>

namespace diagon {
namespace index {

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

    // Increment counters
    numDocsInRAM_++;
    nextDocID_++;

    // Check if flush needed
    return needsFlush();
}

int64_t DocumentsWriterPerThread::bytesUsed() const {
    // Get RAM from terms writer
    int64_t bytes = termsWriter_.bytesUsed();

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

    // Clear field infos
    fieldInfosBuilder_.reset();

    // Reset counters
    numDocsInRAM_ = 0;
    nextDocID_ = 0;
}

}  // namespace index
}  // namespace diagon
