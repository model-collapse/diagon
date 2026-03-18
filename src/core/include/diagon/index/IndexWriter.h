// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/Document.h"
#include "diagon/index/ConcurrentMergeScheduler.h"
#include "diagon/index/DocumentsWriter.h"
#include "diagon/index/MergePolicy.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/Lock.h"
#include "diagon/util/Exceptions.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
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
    // ==================== Format Modes ====================

    /**
     * Controls index file format output.
     *
     * NATIVE:    Diagon-optimized format (PFOR-Delta, TIP6, etc.)
     * OS_COMPAT: OpenSearch/Lucene byte-compatible format (ForUtil, CodecUtil headers, etc.)
     *
     * See: design/16_OPENSEARCH_FORMAT_COMPATIBILITY.md
     */
    enum class FormatMode {
        NATIVE,     // Diagon optimized (default) — codec "Diagon104"
        OS_COMPAT   // OpenSearch/Lucene compatible — codec "Lucene104"
    };

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

    // ==================== Format Mode ====================

    /**
     * Set format mode (default: NATIVE)
     *
     * NATIVE produces Diagon-optimized files (PFOR-Delta, TIP6, etc.)
     * OS_COMPAT produces OpenSearch/Lucene byte-compatible files
     */
    IndexWriterConfig& setFormatMode(FormatMode mode) {
        formatMode_ = mode;
        return *this;
    }

    FormatMode getFormatMode() const { return formatMode_; }

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
    FormatMode formatMode_{FormatMode::NATIVE};
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
     * Add multiple documents in a single batch
     *
     * More efficient than calling addDocument() in a loop because
     * the underlying DocumentsWriter acquires its mutex once for the batch.
     *
     * @param docs Vector of document pointers to add
     * @return sequence number
     */
    int64_t addDocuments(const std::vector<const document::Document*>& docs);

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
     * Wait for all background merges to complete
     */
    void waitForMerges();

    /**
     * Trigger merge evaluation (maybeMerge with SEGMENT_FLUSH trigger).
     * Call after bulk indexing to cascade remaining segments.
     */
    void triggerMerge();

    /**
     * Persist merge results to segments_N without flushing or triggering new merges.
     *
     * After waitForMerges() completes, the in-memory segmentInfos_ reflects
     * the merged state but the on-disk segments_N may be stale. This method
     * writes a new segments_N, cleans up source segment files, and increments
     * the generation — without flushing buffered docs or triggering new merges.
     *
     * Typical usage:
     *   writer.commit();              // flush + write segments_N + trigger merges
     *   writer.waitForMerges();       // wait for background merges
     *   writer.commitMergeResults();  // persist merge results
     *
     * @return sequence number
     */
    int64_t commitMergeResults();

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
    bool useCompoundFile_;
    IndexWriterConfig::OpenMode openMode_;
    IndexWriterConfig::FormatMode formatMode_;
    std::string codecName_;  // Codec name for new segments ("Diagon104" or "Lucene104")
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

    // Background merge support
    ConcurrentMergeScheduler mergeScheduler_;
    mutable std::mutex
        segmentLock_;  // Protects segmentInfos_, mergingSegments_, pendingDeleteSegments_
    std::set<std::string> mergingSegments_;  // Segments currently being merged in background
    std::vector<std::shared_ptr<SegmentInfo>> pendingDeleteSegments_;  // Deferred file deletion
    std::atomic<int> mergeCounter_{0};  // Unique name counter for merged segments
    size_t collectedSegmentCount_{0};   // How many segments from documentsWriter_ already collected
    void maybeMerge(MergeTrigger trigger);
    void collectNewSegments();

    // Helper methods
    void ensureOpen() const;
    int64_t nextSequenceNumber();
    void initializeIndex();
    void writeSegmentsFile();
    void applyDeletes(const Term& term);
    void deleteSegmentFiles(std::shared_ptr<SegmentInfo> segment);
    void deleteOldSegmentsFiles(int64_t currentGeneration);  // Remove stale segments_N files
    std::vector<std::string>
    createCompoundFile(std::shared_ptr<SegmentInfo> segment);  // Pack segment into .cfs/.cfe
    int64_t commitInternal();                      // Internal commit (caller must hold commitLock_)
    void executeMerges(MergeSpecification* spec);  // Execute a set of merges
    void writeSegmentsFileLucene();                // Write segments_N in Lucene format (OS_COMPAT)
};

}  // namespace index
}  // namespace diagon
