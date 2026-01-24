// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"

#include "diagon/store/IndexOutput.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace diagon {
namespace index {

// ==================== IndexWriterConfig ====================

IndexWriterConfig::IndexWriterConfig() = default;

// ==================== IndexWriter ====================

IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : directory_(dir)
    , config_(config) {
    // Obtain write lock
    try {
        writeLock_ = directory_.obtainLock("write.lock");
    } catch (const LockObtainFailedException&) {
        throw LockObtainFailedException("Cannot obtain write lock - another IndexWriter is open");
    }

    // Initialize index
    initializeIndex();

    // Create DocumentsWriter
    DocumentsWriter::Config dwConfig;
    dwConfig.dwptConfig.ramBufferSizeMB = static_cast<int64_t>(config_.getRAMBufferSizeMB());
    if (config_.getMaxBufferedDocs() > 0) {
        dwConfig.dwptConfig.maxBufferedDocs = config_.getMaxBufferedDocs();
    }
    documentsWriter_ = std::make_unique<DocumentsWriter>(dwConfig, &directory_);
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

int64_t IndexWriter::addDocument(const document::Document& doc) {
    ensureOpen();

    // Add document to DocumentsWriter
    // Returns number of segments created (0 or 1 for auto-flush)
    int segmentsCreated = documentsWriter_->addDocument(doc);

    // If segments were created, add them to SegmentInfos
    if (segmentsCreated > 0) {
        for (const auto& segmentInfo : documentsWriter_->getSegmentInfos()) {
            // Check if this segment is new (not already in segmentInfos_)
            bool isNew = true;
            for (int i = 0; i < segmentInfos_.size(); i++) {
                if (segmentInfos_.info(i)->name() == segmentInfo->name()) {
                    isNew = false;
                    break;
                }
            }
            if (isNew) {
                segmentInfos_.add(segmentInfo);
            }
        }
    }

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

    // Flush pending documents
    flush();

    // Write segments_N file
    writeSegmentsFile();

    // Sync directory metadata
    directory_.syncMetaData();

    // Increment generation for next commit
    segmentInfos_.incrementGeneration();

    return nextSequenceNumber();
}

void IndexWriter::flush() {
    ensureOpen();

    // Flush DocumentsWriter (creates segment files)
    int segmentsCreated = documentsWriter_->flush();

    // Add new segments to SegmentInfos
    if (segmentsCreated > 0) {
        for (const auto& segmentInfo : documentsWriter_->getSegmentInfos()) {
            // Check if this segment is new
            bool isNew = true;
            for (int i = 0; i < segmentInfos_.size(); i++) {
                if (segmentInfos_.info(i)->name() == segmentInfo->name()) {
                    isNew = false;
                    break;
                }
            }
            if (isNew) {
                segmentInfos_.add(segmentInfo);
            }
        }
    }
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

int IndexWriter::getNumDocsInRAM() const {
    return documentsWriter_ ? documentsWriter_->getNumDocsInRAM() : 0;
}

int IndexWriter::getNumDocsAdded() const {
    return documentsWriter_ ? documentsWriter_->getNumDocsAdded() : 0;
}

void IndexWriter::initializeIndex() {
    // Determine if index exists and find max generation
    auto files = directory_.listAll();
    bool indexExists = false;
    int64_t maxGeneration = -1;

    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            indexExists = true;
            // Parse generation from filename (hex format after "segments_")
            std::string genStr = file.substr(9);  // Skip "segments_"
            if (!genStr.empty()) {
                try {
                    int64_t gen = std::stoll(genStr, nullptr, 16);
                    maxGeneration = std::max(maxGeneration, gen);
                } catch (...) {
                    // Ignore files with invalid generation format
                }
            }
        }
    }

    // Handle open mode
    switch (config_.getOpenMode()) {
        case IndexWriterConfig::OpenMode::CREATE:
            // Clear existing index if present
            if (indexExists) {
                // Delete all segments_N files
                for (const auto& file : files) {
                    if (file.find("segments_") == 0) {
                        directory_.deleteFile(file);
                    }
                }
            }
            // Start fresh
            segmentInfos_ = SegmentInfos();
            break;

        case IndexWriterConfig::OpenMode::APPEND:
            if (!indexExists) {
                throw IOException("Index does not exist - cannot append");
            }
            // Start from next generation after existing index
            segmentInfos_ = SegmentInfos();
            if (maxGeneration >= 0) {
                // Set generation to next value
                for (int64_t i = 0; i <= maxGeneration; i++) {
                    segmentInfos_.incrementGeneration();
                }
            }
            break;

        case IndexWriterConfig::OpenMode::CREATE_OR_APPEND:
            if (!indexExists) {
                // Create new index
                segmentInfos_ = SegmentInfos();
            } else {
                // Append to existing index - start from next generation
                segmentInfos_ = SegmentInfos();
                if (maxGeneration >= 0) {
                    // Set generation to next value
                    for (int64_t i = 0; i <= maxGeneration; i++) {
                        segmentInfos_.incrementGeneration();
                    }
                }
            }
            break;
    }
}

void IndexWriter::writeSegmentsFile() {
    // Generate segments_N filename
    std::string filename = SegmentInfos::getSegmentsFileName(segmentInfos_.getGeneration());

    // Create output
    auto output = directory_.createOutput(filename, store::IOContext::DEFAULT);

    // Write magic header
    output->writeInt(0x3fd76c17);  // Lucene segments file magic

    // Write version
    output->writeInt(1);

    // Write generation
    output->writeLong(segmentInfos_.getGeneration());

    // Write number of segments
    output->writeInt(segmentInfos_.size());

    // Write each segment info
    for (int i = 0; i < segmentInfos_.size(); i++) {
        auto segmentInfo = segmentInfos_.info(i);

        // Write segment name
        output->writeString(segmentInfo->name());

        // Write maxDoc
        output->writeInt(segmentInfo->maxDoc());

        // Write codec name
        output->writeString(segmentInfo->codecName());

        // Write number of files
        output->writeInt(static_cast<int>(segmentInfo->files().size()));

        // Write file names
        for (const auto& file : segmentInfo->files()) {
            output->writeString(file);
        }

        // Write diagnostics count
        output->writeInt(static_cast<int>(segmentInfo->diagnostics().size()));

        // Write diagnostics
        for (const auto& [key, value] : segmentInfo->diagnostics()) {
            output->writeString(key);
            output->writeString(value);
        }

        // Write size in bytes
        output->writeLong(segmentInfo->sizeInBytes());

        // Write FieldInfos (Phase 4)
        const auto& fieldInfos = segmentInfo->fieldInfos();
        output->writeInt(static_cast<int32_t>(fieldInfos.size()));

        for (const auto& fieldInfo : fieldInfos) {
            // Write field name and number
            output->writeString(fieldInfo.name);
            output->writeInt(fieldInfo.number);

            // Write index options
            output->writeInt(static_cast<int32_t>(fieldInfo.indexOptions));

            // Write doc values type
            output->writeInt(static_cast<int32_t>(fieldInfo.docValuesType));

            // Write flags as booleans
            output->writeByte(fieldInfo.omitNorms ? 1 : 0);
            output->writeByte(fieldInfo.storeTermVector ? 1 : 0);
            output->writeByte(fieldInfo.storePayloads ? 1 : 0);
        }
    }

    // Close output
    output->close();

    // Sync the file
    directory_.sync({filename});
}

}  // namespace index
}  // namespace diagon
