// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/DocumentsWriter.h"
#include "diagon/index/MergePolicy.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/Lock.h"
#include "diagon/util/Exceptions.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace diagon {
namespace index {

using namespace diagon::store;

// Forward declarations
class Term;  // Defined in Term.h
class Query;

// ==================== IndexWriterConfig ====================

/**
 * Configuration for IndexWriter
 *
 * Based on: org.apache.lucene.index.IndexWriterConfig
 */
class IndexWriterConfig {
public:
    // ==================== Open Modes ====================

    enum class OpenMode {
        CREATE,           // Create new index, overwrite existing
        APPEND,           // Open existing, fail if doesn't exist
        CREATE_OR_APPEND  // Create if missing, append otherwise
    };

    // ==================== Construction ====================

    IndexWriterConfig();

    // ==================== RAM Buffer ====================

    /**
     * RAM buffer size in MB (default: 16MB)
     * Flush when exceeded
     */
    IndexWriterConfig& setRAMBufferSizeMB(double mb) {
        ramBufferSizeMB_ = mb;
        return *this;
    }

    double getRAMBufferSizeMB() const { return ramBufferSizeMB_; }

    /**
     * Max buffered docs (default: disabled/-1)
     * Flush when exceeded
     */
    IndexWriterConfig& setMaxBufferedDocs(int max) {
        maxBufferedDocs_ = max;
        return *this;
    }

    int getMaxBufferedDocs() const { return maxBufferedDocs_; }

    // ==================== Open Mode ====================

    IndexWriterConfig& setOpenMode(OpenMode mode) {
        openMode_ = mode;
        return *this;
    }

    OpenMode getOpenMode() const { return openMode_; }

    // ==================== Commit ====================

    /**
     * Commit on close (default: true)
     */
    IndexWriterConfig& setCommitOnClose(bool commit) {
        commitOnClose_ = commit;
        return *this;
    }

    bool getCommitOnClose() const { return commitOnClose_; }

    // ==================== Use Compound File ====================

    /**
     * Use compound file format (default: true)
     */
    IndexWriterConfig& setUseCompoundFile(bool useCompound) {
        useCompoundFile_ = useCompound;
        return *this;
    }

    bool getUseCompoundFile() const { return useCompoundFile_; }

    // ==================== Merge Policy ====================

    /**
     * Set merge policy (default: TieredMergePolicy)
     */
    IndexWriterConfig& setMergePolicy(std::unique_ptr<MergePolicy> policy) {
        mergePolicy_ = std::move(policy);
        return *this;
    }

    MergePolicy* getMergePolicy() const { return mergePolicy_.get(); }

private:
    double ramBufferSizeMB_{16.0};
    int maxBufferedDocs_{-1};  // Disabled
    OpenMode openMode_{OpenMode::CREATE_OR_APPEND};
    bool commitOnClose_{true};
    bool useCompoundFile_{true};
    std::unique_ptr<MergePolicy> mergePolicy_;  // nullptr = use default TieredMergePolicy
};

// ==================== IndexWriter ====================

/**
 * IndexWriter creates and maintains an index.
 *
 * Thread safety:
 * - Multiple threads can add/update/delete documents concurrently
 * - Writer uses internal locking
 * - Single writer per index directory (enforced by write lock)
 *
 * Based on: org.apache.lucene.index.IndexWriter
 *
 * NOTE: This is a minimal implementation focusing on infrastructure and lifecycle.
 * Full document indexing capabilities depend on Codec architecture (Task #7) and
 * will be completed after codec implementation.
 */
class IndexWriter {
public:
    // ==================== Construction ====================

    /**
     * Create writer
     * @param dir Directory for index
     * @param config Configuration (copied, not shared)
     * @throws LockObtainFailedException if cannot obtain write lock
     */
    IndexWriter(Directory& dir, const IndexWriterConfig& config);

    /**
     * Destructor - closes writer if not already closed
     */
    ~IndexWriter();

    // Disable copy/move
    IndexWriter(const IndexWriter&) = delete;
    IndexWriter& operator=(const IndexWriter&) = delete;
    IndexWriter(IndexWriter&&) = delete;
    IndexWriter& operator=(IndexWriter&&) = delete;

    // ==================== Document Operations ====================

    /**
     * Add a document
     * @param doc Document to add
     * @return sequence number (transient, for ordering)
     */
    int64_t addDocument(const document::Document& doc);

    /**
     * Delete all documents matching the given term
     * @param term Term to match for deletion
     * @return sequence number
     */
    int64_t deleteDocuments(const Term& term);

    /**
     * Update document (delete by term, then add new document atomically)
     * Atomic at the segment level: all matching documents are deleted, then new document is added
     * @param term Term to match for deletion (identifies documents to replace)
     * @param doc New document to add
     * @return sequence number
     */
    int64_t updateDocument(const Term& term, const document::Document& doc);

    // ==================== Commit & Merge ====================

    /**
     * Commit changes (flush + sync)
     * Writes segments_N file to disk
     * @return sequence number
     */
    int64_t commit();

    /**
     * Flush pending documents to segments
     * Does not write segments_N file (use commit for that)
     */
    void flush();

    /**
     * Rollback uncommitted changes
     *
     * NOTE: Stub implementation - placeholder for future implementation
     */
    void rollback();

    /**
     * Force merge to at most maxNumSegments
     *
     * NOTE: Stub implementation - placeholder for future implementation
     */
    void forceMerge(int maxNumSegments);

    /**
     * Wait for merges to complete
     *
     * NOTE: Stub implementation - placeholder for future implementation
     */
    void waitForMerges();

    // ==================== Configuration ====================

    // Note: IndexWriterConfig is no longer stored to avoid copy issues with unique_ptr
    // Individual config values are extracted and stored during construction

    // ==================== Statistics ====================

    /**
     * Get current sequence number
     */
    int64_t getSequenceNumber() const { return nextSeqNo_.load(std::memory_order_relaxed); }

    /**
     * Get number of documents in RAM buffer
     */
    int getNumDocsInRAM() const;

    /**
     * Get total number of documents added
     */
    int getNumDocsAdded() const;

    /**
     * Get segment infos (for testing)
     */
    const SegmentInfos& getSegmentInfos() const { return segmentInfos_; }

    // ==================== Lifecycle ====================

    /**
     * Check if closed
     */
    bool isOpen() const { return !closed_.load(std::memory_order_acquire); }

    /**
     * Close writer (commits if commitOnClose is true)
     */
    void close();

private:
    // Configuration
    Directory& directory_;
    bool commitOnClose_;
    IndexWriterConfig::OpenMode openMode_;
    std::unique_ptr<Lock> writeLock_;

    // Indexing pipeline
    std::unique_ptr<DocumentsWriter> documentsWriter_;
    SegmentInfos segmentInfos_;

    // Merge policy
    std::unique_ptr<MergePolicy> mergePolicy_;

    // Sequence numbers
    std::atomic<int64_t> nextSeqNo_{1};

    // Lifecycle
    std::atomic<bool> closed_{false};
    mutable std::mutex commitLock_;
    mutable std::mutex closeLock_;

    // Helper methods
    void ensureOpen() const;
    int64_t nextSequenceNumber();
    void initializeIndex();
    void writeSegmentsFile();
    void applyDeletes(const Term& term);
    void deleteSegmentFiles(std::shared_ptr<SegmentInfo> segment);
    int64_t commitInternal();                      // Internal commit (caller must hold commitLock_)
    void executeMerges(MergeSpecification* spec);  // Execute a set of merges
};

}  // namespace index
}  // namespace diagon
