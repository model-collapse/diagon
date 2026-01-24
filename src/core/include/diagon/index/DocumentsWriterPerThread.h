// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/FreqProxTermsWriter.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"

#include <atomic>
#include <memory>
#include <string>

namespace diagon {
namespace index {

/**
 * DocumentsWriterPerThread (DWPT) - Per-thread document buffer
 *
 * Based on: org.apache.lucene.index.DocumentsWriterPerThread
 *
 * Each DWPT is owned by a single thread and buffers documents in RAM
 * until a flush is triggered (by RAM limit or document count).
 *
 * Design:
 * - Owns FreqProxTermsWriter for building posting lists
 * - Owns FieldInfosBuilder for tracking field metadata
 * - Tracks document count and RAM usage
 * - Provides flush() to create segment
 *
 * Thread Safety:
 * - NOT thread-safe - owned by single thread
 * - DocumentsWriter coordinates multiple DWPTs
 *
 * Lifecycle:
 * 1. Create DWPT
 * 2. addDocument() repeatedly
 * 3. flush() when RAM limit reached
 * 4. reset() for reuse
 *
 * Memory Tracking:
 * - Posting lists in FreqProxTermsWriter
 * - Field metadata in FieldInfosBuilder
 * - Document storage (simplified for Phase 3)
 */
class DocumentsWriterPerThread {
public:
    /**
     * Configuration for DWPT
     */
    struct Config {
        int64_t ramBufferSizeMB;  // RAM limit in MB
        int maxBufferedDocs;      // Max docs before flush

        // Default constructor with default values
        Config() : ramBufferSizeMB(16), maxBufferedDocs(1000) {}
    };

    /**
     * Default constructor (uses default config)
     */
    DocumentsWriterPerThread();

    /**
     * Constructor with custom config
     *
     * @param config Configuration
     * @param directory Directory for writing segment files (optional for Phase 3)
     * @param codecName Codec name (default: "Lucene104")
     */
    explicit DocumentsWriterPerThread(
        const Config& config,
        store::Directory* directory = nullptr,
        const std::string& codecName = "Lucene104");

    /**
     * Destructor
     */
    ~DocumentsWriterPerThread() = default;

    // Disable copy/move
    DocumentsWriterPerThread(const DocumentsWriterPerThread&) = delete;
    DocumentsWriterPerThread& operator=(const DocumentsWriterPerThread&) = delete;
    DocumentsWriterPerThread(DocumentsWriterPerThread&&) = delete;
    DocumentsWriterPerThread& operator=(DocumentsWriterPerThread&&) = delete;

    /**
     * Add document to buffer
     *
     * @param doc Document to add
     * @return True if flush needed after this document
     */
    bool addDocument(const document::Document& doc);

    /**
     * Get number of documents in RAM buffer
     */
    int getNumDocsInRAM() const {
        return numDocsInRAM_;
    }

    /**
     * Get approximate bytes used
     */
    int64_t bytesUsed() const;

    /**
     * Check if flush is needed
     * Based on RAM or document count limits
     */
    bool needsFlush() const;

    /**
     * Flush to segment
     * Returns SegmentInfo, or nullptr if nothing to flush
     *
     * Phase 3 Task 4: Invokes codec to write posting lists
     * Phase 4: Will add compression, FST term dictionary, skip lists
     */
    std::shared_ptr<SegmentInfo> flush();

    /**
     * Reset for reuse
     * Clears all buffered data
     */
    void reset();

    /**
     * Get field infos builder (for testing)
     */
    const FieldInfosBuilder& getFieldInfosBuilder() const {
        return fieldInfosBuilder_;
    }

    /**
     * Get terms writer (for testing)
     */
    const FreqProxTermsWriter& getTermsWriter() const {
        return termsWriter_;
    }

private:
    // Configuration
    Config config_;

    // Field metadata tracker
    FieldInfosBuilder fieldInfosBuilder_;

    // In-memory posting list builder
    FreqProxTermsWriter termsWriter_;

    // Document count in RAM
    int numDocsInRAM_{0};

    // Next document ID (segment-local)
    int nextDocID_{0};

    // Directory for writing segment files
    store::Directory* directory_{nullptr};

    // Codec name
    std::string codecName_;

    // Segment generation counter (for flush) - atomic for thread safety
    static std::atomic<int> nextSegmentNumber_;
};

}  // namespace index
}  // namespace diagon
