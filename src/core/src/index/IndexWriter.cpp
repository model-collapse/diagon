// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"

#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/index/MergeSpecification.h"
#include "diagon/index/OneMerge.h"
#include "diagon/index/SegmentMerger.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/index/Term.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/index/TieredMergePolicy.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/store/IndexOutput.h"
#include "diagon/util/BitSet.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace diagon {
namespace index {

// ==================== IndexWriterConfig ====================

IndexWriterConfig::IndexWriterConfig() = default;

// ==================== IndexWriter ====================

IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : directory_(dir)
    , commitOnClose_(config.getCommitOnClose())
    , openMode_(config.getOpenMode()) {
    // Obtain write lock
    try {
        writeLock_ = directory_.obtainLock("write.lock");
    } catch (const LockObtainFailedException&) {
        throw LockObtainFailedException("Cannot obtain write lock - another IndexWriter is open");
    }

    // Initialize index
    initializeIndex();

    // Initialize merge policy (use provided or create default TieredMergePolicy)
    if (config.getMergePolicy()) {
        // Use user-provided policy (clone it)
        mergePolicy_ = std::make_unique<TieredMergePolicy>(*static_cast<TieredMergePolicy*>(config.getMergePolicy()));
    } else {
        // Use default TieredMergePolicy
        mergePolicy_ = std::make_unique<TieredMergePolicy>();
    }

    // Create DocumentsWriter
    DocumentsWriter::Config dwConfig;
    dwConfig.dwptConfig.ramBufferSizeMB = static_cast<int64_t>(config.getRAMBufferSizeMB());
    if (config.getMaxBufferedDocs() > 0) {
        dwConfig.dwptConfig.maxBufferedDocs = config.getMaxBufferedDocs();
    }
    documentsWriter_ = std::make_unique<DocumentsWriter>(dwConfig, &directory_);
}

IndexWriter::~IndexWriter() {
    if (isOpen()) {
        try {
            if (commitOnClose_) {
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

int64_t IndexWriter::deleteDocuments(const Term& term) {
    ensureOpen();

    // Apply deletions to all existing segments
    applyDeletes(term);

    return nextSequenceNumber();
}

int64_t IndexWriter::updateDocument(const Term& term, const document::Document& doc) {
    ensureOpen();

    // Delete old documents matching term
    applyDeletes(term);

    // Add new document
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

int64_t IndexWriter::commit() {
    ensureOpen();

    std::lock_guard<std::mutex> lock(commitLock_);

    return commitInternal();
}

int64_t IndexWriter::commitInternal() {
    // Internal commit implementation (caller must hold commitLock_)

    // Flush pending documents
    flush();

    // Check for segments with high deletions and merge them
    std::unique_ptr<MergeSpecification> deletesMerges(
        mergePolicy_->findForcedDeletesMerges(segmentInfos_));
    if (deletesMerges && !deletesMerges->empty()) {
        executeMerges(deletesMerges.get());
    }

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

    std::lock_guard<std::mutex> lock(commitLock_);

    // 1. Discard all pending documents in DocumentsWriter
    documentsWriter_->reset();

    // 2. Reset to last committed state (if exists)
    try {
        // Try to read the latest commit from disk
        auto committedInfos = SegmentInfos::readLatestCommit(directory_);

        // Replace in-memory segment list with committed state
        segmentInfos_ = committedInfos;
    } catch (const IOException&) {
        // No committed state exists - this is a new index
        // Clear all segments and start fresh
        segmentInfos_.clear();
    }

    // 3. Close without committing
    closed_.store(true, std::memory_order_release);

    // Release write lock
    if (writeLock_) {
        writeLock_->close();
        writeLock_.reset();
    }
}

void IndexWriter::forceMerge(int maxNumSegments) {
    ensureOpen();

    if (maxNumSegments < 1) {
        throw std::invalid_argument("maxNumSegments must be >= 1");
    }

    std::lock_guard<std::mutex> lock(commitLock_);

    // 1. Flush all pending documents first
    flush();

    // 2. Use TieredMergePolicy to find forced merges
    std::unique_ptr<MergeSpecification> spec(
        mergePolicy_->findForcedMerges(segmentInfos_, maxNumSegments, {}));

    // 3. Execute merges if any were found
    if (spec && !spec->empty()) {
        executeMerges(spec.get());
    }

    // 4. Commit the merged index (use internal method since we already hold the lock)
    commitInternal();
}

void IndexWriter::deleteSegmentFiles(std::shared_ptr<SegmentInfo> segment) {
    // Delete all files belonging to this segment
    for (const auto& file : segment->files()) {
        try {
            directory_.deleteFile(file);
        } catch (const std::exception&) {
            // Ignore errors - file might already be deleted or not exist
        }
    }

    // Also try to delete the .liv file if it exists
    if (segment->hasDeletions()) {
        try {
            std::string livFileName = segment->name() + ".liv";
            directory_.deleteFile(livFileName);
        } catch (const std::exception&) {
            // Ignore errors
        }
    }
}

void IndexWriter::waitForMerges() {
    ensureOpen();

    // Phase 2 implementation: All merges are synchronous (via forceMerge)
    // This method is a no-op until background merging is implemented in Phase 4
    // with MergeScheduler and asynchronous merge threads
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
    switch (openMode_) {
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
            // Load existing index and prepare for next generation
            segmentInfos_ = SegmentInfos::readLatestCommit(directory_);
            segmentInfos_.incrementGeneration();
            break;

        case IndexWriterConfig::OpenMode::CREATE_OR_APPEND:
            if (!indexExists) {
                // Create new index
                segmentInfos_ = SegmentInfos();
            } else {
                // Load existing index and prepare for next generation
                segmentInfos_ = SegmentInfos::readLatestCommit(directory_);
                segmentInfos_.incrementGeneration();
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

        // Write delCount (Phase 3)
        output->writeInt(segmentInfo->delCount());

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

void IndexWriter::applyDeletes(const Term& term) {
    // Apply deletions to all existing segments
    for (int i = 0; i < segmentInfos_.size(); i++) {
        auto segmentInfo = segmentInfos_.info(i);

        try {
            // Open segment reader
            auto reader = SegmentReader::open(directory_, segmentInfo);

            // Get terms for the field
            Terms* terms = reader->terms(term.field());
            if (!terms) {
                continue;  // Field doesn't exist in this segment
            }

            // Get terms enum and seek to term
            std::unique_ptr<TermsEnum> termsEnum = terms->iterator();
            if (!termsEnum->seekExact(term.bytes())) {
                continue;  // Term doesn't exist in this segment
            }

            // Get postings enum to iterate over matching documents
            std::unique_ptr<PostingsEnum> postings = termsEnum->postings();
            if (!postings) {
                continue;
            }

            // Load or create live docs bitset
            std::unique_ptr<util::BitSet> liveDocs;
            int maxDoc = segmentInfo->maxDoc();

            // Check if segment already has deletions
            if (segmentInfo->hasDeletions()) {
                // Load existing live docs
                codecs::LiveDocsFormat format;
                liveDocs = format.readLiveDocs(directory_, segmentInfo->name(), maxDoc);
                if (!liveDocs) {
                    // Failed to load - create new one
                    liveDocs = std::make_unique<util::BitSet>(maxDoc);
                    for (int d = 0; d < maxDoc; d++) {
                        liveDocs->set(d);  // All live initially
                    }
                }
            } else {
                // No deletions yet - create new bitset with all docs live
                liveDocs = std::make_unique<util::BitSet>(maxDoc);
                for (int d = 0; d < maxDoc; d++) {
                    liveDocs->set(d);
                }
            }

            // Collect documents to delete
            int deletedCount = 0;
            int docID;
            while ((docID = postings->nextDoc()) != PostingsEnum::NO_MORE_DOCS) {
                if (liveDocs->get(docID)) {
                    liveDocs->clear(docID);  // Mark as deleted
                    deletedCount++;
                }
            }

            if (deletedCount > 0) {
                // Update delCount
                int newDelCount = segmentInfo->delCount() + deletedCount;
                segmentInfo->setDelCount(newDelCount);

                // Write updated .liv file
                codecs::LiveDocsFormat format;
                format.writeLiveDocs(directory_, segmentInfo->name(), *liveDocs, newDelCount);
            }

            // Close reader
            reader->decRef();

        } catch (const std::exception& e) {
            // Segment might be corrupted or missing - skip it
            continue;
        }
    }
}

void IndexWriter::executeMerges(MergeSpecification* spec) {
    // Execute each merge in the specification
    // Note: This is a synchronous implementation. Background merging would be added in Phase 4
    // with MergeScheduler and separate merge threads.

    static int mergeCounter = 0;

    for (const auto& oneMerge : spec->getMerges()) {
        // Get segments to merge (cast from SegmentCommitInfo* back to SegmentInfo*)
        std::vector<std::shared_ptr<SegmentInfo>> segmentsToMerge;
        for (auto* segCommit : oneMerge->getSegments()) {
            // Cast back to SegmentInfo* (temporary until SegmentCommitInfo is implemented)
            auto* segInfo = reinterpret_cast<SegmentInfo*>(segCommit);

            // Find the matching segment in segmentInfos_
            for (int i = 0; i < segmentInfos_.size(); i++) {
                if (segmentInfos_.info(i)->name() == segInfo->name()) {
                    segmentsToMerge.push_back(segmentInfos_.info(i));
                    break;
                }
            }
        }

        if (segmentsToMerge.size() < 2) {
            continue;  // Need at least 2 segments to merge
        }

        // Generate name for merged segment
        std::string mergedName = "_merged_" + std::to_string(mergeCounter++);

        // Perform merge using SegmentMerger
        SegmentMerger merger(directory_, mergedName, segmentsToMerge);
        auto mergedSegment = merger.merge();

        // Remove old segments from list (iterate backwards to avoid index shifts)
        for (int i = segmentInfos_.size() - 1; i >= 0; i--) {
            auto segment = segmentInfos_.info(i);
            for (const auto& toMerge : segmentsToMerge) {
                if (segment->name() == toMerge->name()) {
                    segmentInfos_.remove(i);
                    break;
                }
            }
        }

        // Add merged segment to list
        segmentInfos_.add(mergedSegment);

        // Delete old segment files
        for (const auto& segment : segmentsToMerge) {
            deleteSegmentFiles(segment);
        }
    }
}

}  // namespace index
}  // namespace diagon
