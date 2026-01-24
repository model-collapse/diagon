// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"

#include <stdexcept>

namespace diagon {
namespace index {

// ==================== IndexWriterConfig ====================

IndexWriterConfig::IndexWriterConfig() = default;

// ==================== IndexWriter ====================

IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : directory_(dir), config_(config) {

    // Obtain write lock
    try {
        writeLock_ = directory_.obtainLock("write.lock");
    } catch (const LockObtainFailedException&) {
        throw LockObtainFailedException(
            "Cannot obtain write lock - another IndexWriter is open");
    }

    // TODO: When codec architecture is available:
    // - Read existing segments_N file (if APPEND or CREATE_OR_APPEND)
    // - Initialize DocumentsWriter
    // - Initialize ReaderPool
    // - Initialize MergeScheduler
}

IndexWriter::~IndexWriter() {
    if (isOpen()) {
        try {
            if (config_.getCommitOnClose()) {
                commit();
            }
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

int64_t IndexWriter::addDocument() {
    ensureOpen();

    // TODO: When Document class and Codec are available:
    // - Add document to DocumentsWriter
    // - Check flush triggers (RAM buffer, max docs)
    // - Trigger flush if needed

    return nextSequenceNumber();
}

int64_t IndexWriter::updateDocument() {
    ensureOpen();

    // TODO: When Term, Document, and Codec are available:
    // - Apply deletion to matching term
    // - Add new document (atomic within segment)

    return nextSequenceNumber();
}

int64_t IndexWriter::deleteDocuments() {
    ensureOpen();

    // TODO: When Term/Query and Codec are available:
    // - Apply deletion to matching documents
    // - Update deletion markers

    return nextSequenceNumber();
}

int64_t IndexWriter::commit() {
    ensureOpen();

    std::lock_guard<std::mutex> lock(commitLock_);

    // TODO: When codec architecture is available:
    // - Flush pending documents
    // - Write segments_N file
    // - Sync all files
    // - Update commit generation

    return nextSequenceNumber();
}

void IndexWriter::flush() {
    ensureOpen();

    // TODO: When codec architecture is available:
    // - Flush DocumentsWriter
    // - Write segment files
    // - Do not write segments_N (that's commit's job)
}

void IndexWriter::rollback() {
    ensureOpen();

    // TODO: When codec architecture is available:
    // - Discard pending changes in DocumentsWriter
    // - Revert to last committed state
    // - Close writer
}

void IndexWriter::forceMerge(int maxNumSegments) {
    ensureOpen();

    if (maxNumSegments < 1) {
        throw std::invalid_argument("maxNumSegments must be >= 1");
    }

    // TODO: When merge infrastructure is available:
    // - Trigger merge policy to merge down to maxNumSegments
    // - Wait for merges to complete
}

void IndexWriter::waitForMerges() {
    ensureOpen();

    // TODO: When MergeScheduler is available:
    // - Wait for all pending merges to complete
}

void IndexWriter::close() {
    std::lock_guard<std::mutex> lock(closeLock_);

    if (closed_.load(std::memory_order_acquire)) {
        return;  // Already closed
    }

    // Release write lock
    if (writeLock_) {
        writeLock_->close();
        writeLock_.reset();
    }

    // Mark as closed
    closed_.store(true, std::memory_order_release);
}

void IndexWriter::ensureOpen() const {
    if (!isOpen()) {
        throw AlreadyClosedException("IndexWriter is closed");
    }
}

int64_t IndexWriter::nextSequenceNumber() {
    return nextSeqNo_.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace index
}  // namespace diagon
