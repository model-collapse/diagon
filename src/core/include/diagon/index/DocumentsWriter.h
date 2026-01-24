// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace diagon {
namespace index {

/**
 * DocumentsWriter - Coordinates document buffering and segment creation
 *
 * Based on: org.apache.lucene.index.DocumentsWriter
 *
 * Manages the indexing pipeline:
 * - Owns pool of DocumentsWriterPerThread (DWPT) instances
 * - Routes documents to DWPTs
 * - Coordinates flushing when RAM/doc limits reached
 * - Tracks created segments
 *
 * Phase 3 Design:
 * - Single DWPT for simplicity
 * - Auto-flush when DWPT reaches limits
 * - Track segment names created
 *
 * Phase 4 Enhancement:
 * - Multiple DWPTs (thread-local)
 * - Thread assignment and load balancing
 * - Concurrent flushing
 *
 * Thread Safety:
 * - Mutex protects all access to dwpt_ and internal state
 * - Safe for concurrent addDocument() calls
 *
 * Usage:
 *   DocumentsWriter writer;
 *   writer.addDocument(doc);  // Buffers in RAM
 *   writer.flush();           // Flush to segments
 *   auto segments = writer.getSegments();
 */
class DocumentsWriter {
public:
    /**
     * Configuration for DocumentsWriter
     */
    struct Config {
        DocumentsWriterPerThread::Config dwptConfig;  // DWPT configuration
    };

    /**
     * Default constructor
     */
    DocumentsWriter();

    /**
     * Constructor with custom config
     *
     * @param config Configuration
     * @param directory Directory for writing segment files (optional)
     */
    explicit DocumentsWriter(const Config& config, store::Directory* directory = nullptr);

    /**
     * Destructor
     */
    ~DocumentsWriter() = default;

    // Disable copy/move
    DocumentsWriter(const DocumentsWriter&) = delete;
    DocumentsWriter& operator=(const DocumentsWriter&) = delete;
    DocumentsWriter(DocumentsWriter&&) = delete;
    DocumentsWriter& operator=(DocumentsWriter&&) = delete;

    /**
     * Add document to index
     *
     * Routes document to DWPT and flushes if needed.
     *
     * @param doc Document to add
     * @return Number of segments created (0 or 1)
     */
    int addDocument(const document::Document& doc);

    /**
     * Flush all pending documents to segments
     *
     * Forces flush even if limits not reached.
     *
     * @return Number of segments created
     */
    int flush();

    /**
     * Get total documents buffered in RAM
     */
    int getNumDocsInRAM() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dwpt_->getNumDocsInRAM();
    }

    /**
     * Get approximate bytes used
     */
    int64_t bytesUsed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dwpt_->bytesUsed();
    }

    /**
     * Get list of segment names created
     *
     * Returns segment names in creation order.
     */
    std::vector<std::string> getSegments() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return segmentNames_;  // Return copy to avoid holding lock
    }

    /**
     * Get list of SegmentInfo objects
     *
     * Returns SegmentInfo instances in creation order.
     */
    std::vector<std::shared_ptr<SegmentInfo>> getSegmentInfos() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return segments_;  // Return copy to avoid holding lock
    }

    /**
     * Get total number of documents added
     */
    int getNumDocsAdded() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return numDocsAdded_;
    }

    /**
     * Reset for reuse
     * Clears all state including segment list
     */
    void reset();

    /**
     * Check if flush is needed
     */
    bool needsFlush() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dwpt_->needsFlush();
    }

private:
    // Configuration
    Config config_;

    // Active DWPT (Phase 3: single instance)
    std::unique_ptr<DocumentsWriterPerThread> dwpt_;

    // Directory for writing segment files
    store::Directory* directory_{nullptr};

    // Flushed segments (SegmentInfo objects)
    std::vector<std::shared_ptr<SegmentInfo>> segments_;

    // Segment names (for backwards compatibility)
    std::vector<std::string> segmentNames_;

    // Total documents added (across all segments)
    int numDocsAdded_{0};

    // Mutex to protect concurrent access
    mutable std::mutex mutex_;

    /**
     * Flush DWPT if it has documents
     *
     * @return SegmentInfo if flushed, nullptr otherwise
     */
    std::shared_ptr<SegmentInfo> maybeFlushdwpt();
};

}  // namespace index
}  // namespace diagon
