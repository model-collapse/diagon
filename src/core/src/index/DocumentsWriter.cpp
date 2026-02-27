// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/DocumentsWriter.h"

namespace diagon {
namespace index {

DocumentsWriter::DocumentsWriter()
    : config_(Config{})
    , dwpt_(std::make_unique<DocumentsWriterPerThread>(config_.dwptConfig, nullptr, "Lucene104")) {}

DocumentsWriter::DocumentsWriter(const Config& config, store::Directory* directory)
    : config_(config)
    , dwpt_(std::make_unique<DocumentsWriterPerThread>(config_.dwptConfig, directory, "Lucene104"))
    , directory_(directory) {}

int DocumentsWriter::addDocument(const document::Document& doc) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Add document to DWPT
    bool needsFlush = dwpt_->addDocument(doc);
    numDocsAdded_++;

    // Check if flush needed
    if (needsFlush) {
        // Flush DWPT
        auto segmentInfo = maybeFlushdwpt();
        return segmentInfo ? 1 : 0;
    }

    return 0;
}

int DocumentsWriter::addDocuments(const std::vector<const document::Document*>& docs) {
    std::lock_guard<std::mutex> lock(mutex_);

    int segmentsCreated = 0;
    for (auto* doc : docs) {
        bool needsFlush = dwpt_->addDocument(*doc);
        numDocsAdded_++;

        if (needsFlush) {
            auto segmentInfo = maybeFlushdwpt();
            if (segmentInfo) {
                segmentsCreated++;
            }
        }
    }

    return segmentsCreated;
}

int DocumentsWriter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Flush current DWPT
    auto segmentInfo = maybeFlushdwpt();
    return segmentInfo ? 1 : 0;
}

void DocumentsWriter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Reset DWPT
    dwpt_->reset();

    // Clear segments
    segments_.clear();
    segmentNames_.clear();

    // Reset counters
    numDocsAdded_ = 0;
}

std::shared_ptr<SegmentInfo> DocumentsWriter::maybeFlushdwpt() {
    // Check if DWPT has documents
    if (dwpt_->getNumDocsInRAM() == 0) {
        return nullptr;
    }

    // Flush DWPT
    auto segmentInfo = dwpt_->flush();

    // Track segment
    if (segmentInfo) {
        segments_.push_back(segmentInfo);
        segmentNames_.push_back(segmentInfo->name());
    }

    return segmentInfo;
}

}  // namespace index
}  // namespace diagon
