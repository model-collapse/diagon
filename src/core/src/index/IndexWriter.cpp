// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"

#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/index/MergeSpecification.h"
#include "diagon/index/OneMerge.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentMerger.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/index/Term.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/index/TieredMergePolicy.h"
#include "diagon/store/CompoundFileWriter.h"
#include "diagon/store/IndexOutput.h"
#include "diagon/util/BitSet.h"

#include <atomic>
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
    , useCompoundFile_(config.getUseCompoundFile())
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
        mergePolicy_ = std::make_unique<TieredMergePolicy>(
            *static_cast<TieredMergePolicy*>(config.getMergePolicy()));
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

    // If segments were created, collect them and maybe merge (background)
    if (segmentsCreated > 0) {
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            collectNewSegments();
        }
        maybeMerge(MergeTrigger::SEGMENT_FLUSH);
    }

    return nextSequenceNumber();
}

int64_t IndexWriter::addDocuments(const std::vector<const document::Document*>& docs) {
    ensureOpen();

    int segmentsCreated = documentsWriter_->addDocuments(docs);

    if (segmentsCreated > 0) {
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            collectNewSegments();
        }
        maybeMerge(MergeTrigger::SEGMENT_FLUSH);
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

    // If segments were created, collect them and maybe merge (background)
    if (segmentsCreated > 0) {
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            collectNewSegments();
        }
        maybeMerge(MergeTrigger::SEGMENT_FLUSH);
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

    // Wait for any in-flight background merges to complete before committing.
    // After this, no background merges are running, so segmentInfos_ is stable.
    mergeScheduler_.waitForMerges();

    // Create compound files for newly flushed segments (if enabled).
    // Snapshot segments under lock, then do I/O without lock.
    std::vector<std::vector<std::string>> pendingCompoundDeletes;
    if (useCompoundFile_) {
        std::vector<std::shared_ptr<SegmentInfo>> needCompound;
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            for (int i = 0; i < segmentInfos_.size(); i++) {
                auto segment = segmentInfos_.info(i);
                if (!segment->getUseCompoundFile()) {
                    needCompound.push_back(segment);
                }
            }
        }
        for (auto& segment : needCompound) {
            auto origFiles = createCompoundFile(segment);
            if (!origFiles.empty()) {
                pendingCompoundDeletes.push_back(std::move(origFiles));
            }
        }
    }

    // Size-based tiered merge at commit time (synchronous — must complete before segments_N).
    // findMerges reads segmentInfos_ under lock; executeMerges has its own locking.
    for (int i = 0; i < 10; i++) {
        std::unique_ptr<MergeSpecification> spec;
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            spec.reset(mergePolicy_->findMerges(MergeTrigger::COMMIT, segmentInfos_));
        }
        if (!spec || spec->empty()) {
            break;
        }
        executeMerges(spec.get());
    }

    // Check for segments with high deletions and merge them
    {
        std::unique_ptr<MergeSpecification> deletesMerges;
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            deletesMerges.reset(mergePolicy_->findForcedDeletesMerges(segmentInfos_));
        }
        if (deletesMerges && !deletesMerges->empty()) {
            executeMerges(deletesMerges.get());
        }
    }

    // Write segments_N file — this is the atomic commit point
    {
        std::lock_guard<std::mutex> lock(segmentLock_);
        writeSegmentsFile();
    }

    // Sync directory metadata
    directory_.syncMetaData();

    // Post-commit cleanup: now safe to delete original files replaced by compound
    for (const auto& files : pendingCompoundDeletes) {
        for (const auto& file : files) {
            try {
                directory_.deleteFile(file);
            } catch (const std::exception&) {
                // Best-effort — file might already be gone
            }
        }
    }

    // Delete old segments_N files (keep only the one we just wrote)
    deleteOldSegmentsFiles(segmentInfos_.getGeneration());

    // Increment generation for next commit
    segmentInfos_.incrementGeneration();

    return nextSequenceNumber();
}

void IndexWriter::flush() {
    ensureOpen();

    // Flush DocumentsWriter (creates segment files)
    int segmentsCreated = documentsWriter_->flush();

    // Add new segments to SegmentInfos (no merge — matches Lucene flush behavior)
    if (segmentsCreated > 0) {
        std::lock_guard<std::mutex> lock(segmentLock_);
        collectNewSegments();
    }
}

void IndexWriter::collectNewSegments() {
    auto allSegments = documentsWriter_->getSegmentInfos();
    for (size_t i = collectedSegmentCount_; i < allSegments.size(); i++) {
        segmentInfos_.add(allSegments[i]);
    }
    size_t newCount = allSegments.size();

    // Prune already-collected segments from DocumentsWriter to prevent
    // unbounded growth during long indexing sessions (Phase 1b).
    if (newCount > 0) {
        documentsWriter_->pruneCollectedSegments(newCount);
    }
    collectedSegmentCount_ = 0;  // Reset after prune — DocumentsWriter vector was trimmed
}

void IndexWriter::maybeMerge(MergeTrigger trigger) {
    // Build a filtered SegmentInfos that excludes segments currently being merged.
    // findMerges() is called on this filtered view so it won't select segments that
    // are already being merged in the background.
    SegmentInfos filteredInfos;
    {
        std::lock_guard<std::mutex> lock(segmentLock_);
        for (int i = 0; i < segmentInfos_.size(); i++) {
            auto seg = segmentInfos_.info(i);
            if (mergingSegments_.find(seg->name()) == mergingSegments_.end()) {
                filteredInfos.add(seg);
            }
        }
    }

    // Find merges (no lock — filteredInfos is a local copy)
    std::unique_ptr<MergeSpecification> spec(
        mergePolicy_->findMerges(trigger, filteredInfos));
    if (!spec || spec->empty()) {
        return;
    }

    for (const auto& oneMerge : spec->getMerges()) {
        // Resolve segments under lock, skip if any are already merging
        std::vector<std::shared_ptr<SegmentInfo>> segmentsToMerge;
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            bool skip = false;
            for (auto* segCommit : oneMerge->getSegments()) {
                auto* segInfo = reinterpret_cast<SegmentInfo*>(segCommit);
                if (mergingSegments_.count(segInfo->name())) {
                    skip = true;
                    break;
                }
                for (int i = 0; i < segmentInfos_.size(); i++) {
                    if (segmentInfos_.info(i)->name() == segInfo->name()) {
                        segmentsToMerge.push_back(segmentInfos_.info(i));
                        break;
                    }
                }
            }

            if (skip || segmentsToMerge.size() < 2) {
                continue;
            }

            // Mark segments as merging
            for (auto& s : segmentsToMerge) {
                mergingSegments_.insert(s->name());
            }
        }

        std::string mergedName = "_merged_" + std::to_string(mergeCounter_.fetch_add(1));
        bool useCompound = useCompoundFile_;

        // Submit merge to background thread
        mergeScheduler_.submit([this, segmentsToMerge, mergedName, useCompound]() {
            // Heavy I/O — no lock held
            SegmentMerger merger(directory_, mergedName, segmentsToMerge);
            auto mergedSegment = merger.merge();

            // Compound file (also no lock — just I/O)
            if (useCompound) {
                auto origFiles = createCompoundFile(mergedSegment);
                for (auto& f : origFiles) {
                    try {
                        directory_.deleteFile(f);
                    } catch (...) {
                    }
                }
            }

            // Quick bookkeeping under lock
            {
                std::lock_guard<std::mutex> lock(segmentLock_);
                // Remove old segments from segmentInfos_
                for (int i = segmentInfos_.size() - 1; i >= 0; i--) {
                    for (auto& s : segmentsToMerge) {
                        if (segmentInfos_.info(i)->name() == s->name()) {
                            segmentInfos_.remove(i);
                            break;
                        }
                    }
                }
                segmentInfos_.add(mergedSegment);

                // Unmark from mergingSegments_
                for (auto& s : segmentsToMerge) {
                    mergingSegments_.erase(s->name());
                }
            }

            // Delete old segment files (outside lock)
            for (auto& s : segmentsToMerge) {
                deleteSegmentFiles(s);
            }

            // Trigger cascading: the merged segment may now be eligible
            // for further merging with other segments.  This self-submits
            // to the scheduler, maintaining the cascade loop that the old
            // synchronous maybeMerge() had (up to 10 iterations).
            maybeMerge(MergeTrigger::SEGMENT_FLUSH);
        });
    }
}

void IndexWriter::rollback() {
    ensureOpen();

    std::lock_guard<std::mutex> lock(commitLock_);

    // Stop background merges before modifying state
    mergeScheduler_.shutdown();

    // 1. Discard all pending documents in DocumentsWriter
    documentsWriter_->reset();

    // 2. Reset to last committed state (if exists)
    {
        std::lock_guard<std::mutex> slock(segmentLock_);
        try {
            auto committedInfos = SegmentInfos::readLatestCommit(directory_);
            segmentInfos_ = committedInfos;
        } catch (const IOException&) {
            segmentInfos_.clear();
        }
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

    // 1. Wait for any in-flight background merges
    mergeScheduler_.waitForMerges();

    // 2. Flush all pending documents first
    flush();

    // 3. Use TieredMergePolicy to find forced merges (synchronous)
    std::unique_ptr<MergeSpecification> spec;
    {
        std::lock_guard<std::mutex> slock(segmentLock_);
        spec.reset(mergePolicy_->findForcedMerges(segmentInfos_, maxNumSegments, {}));
    }

    // 4. Execute merges if any were found
    if (spec && !spec->empty()) {
        executeMerges(spec.get());
    }

    // 5. Commit the merged index (use internal method since we already hold the lock)
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

void IndexWriter::deleteOldSegmentsFiles(int64_t currentGeneration) {
    std::string currentFile = SegmentInfos::getSegmentsFileName(currentGeneration);

    auto files = directory_.listAll();
    for (const auto& file : files) {
        if (file.find("segments_") == 0 && file != currentFile) {
            try {
                directory_.deleteFile(file);
            } catch (const std::exception&) {
                // Best-effort deletion — ignore failures
            }
        }
    }
}

std::vector<std::string> IndexWriter::createCompoundFile(std::shared_ptr<SegmentInfo> segment) {
    // Write .cfs/.cfe and update SegmentInfo in-memory.
    // Returns list of original files to delete AFTER commit succeeds (crash-safety).

    const auto& files = segment->files();
    if (files.empty()) {
        return {};
    }

    // Filter out .cfs/.cfe files (shouldn't be there, but be safe)
    std::vector<std::string> filesToPack;
    for (const auto& file : files) {
        if (file.find(".cfs") == std::string::npos && file.find(".cfe") == std::string::npos) {
            filesToPack.push_back(file);
        }
    }

    if (filesToPack.empty()) {
        return {};
    }

    // Write compound file
    store::CompoundFileWriter::write(directory_, segment->name(), filesToPack);

    // Do NOT delete originals here — caller deletes after commit succeeds

    // Update SegmentInfo: replace file list with .cfs/.cfe
    std::vector<std::string> compoundFiles;
    compoundFiles.push_back(store::CompoundFileWriter::getDataFileName(segment->name()));
    compoundFiles.push_back(store::CompoundFileWriter::getEntriesFileName(segment->name()));
    segment->setFiles(compoundFiles);

    // Mark segment as compound
    segment->setUseCompoundFile(true);

    return filesToPack;  // Caller deletes these after commit
}

void IndexWriter::waitForMerges() {
    ensureOpen();
    mergeScheduler_.waitForMerges();
}

void IndexWriter::close() {
    std::lock_guard<std::mutex> lock(closeLock_);

    if (closed_.load(std::memory_order_acquire)) {
        return;  // Already closed
    }

    // Shut down merge scheduler (drains pending merges, joins thread)
    mergeScheduler_.shutdown();

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

        // Write compound file flag
        output->writeByte(segmentInfo->getUseCompoundFile() ? 1 : 0);
    }

    // Close output
    output->close();

    // Sync the file
    directory_.sync({filename});
}

void IndexWriter::applyDeletes(const Term& term) {
    // Snapshot segments under lock (slow I/O follows, don't hold lock)
    std::vector<std::shared_ptr<SegmentInfo>> segments;
    {
        std::lock_guard<std::mutex> lock(segmentLock_);
        for (int i = 0; i < segmentInfos_.size(); i++) {
            segments.push_back(segmentInfos_.info(i));
        }
    }

    // Apply deletions to all existing segments
    for (auto& segmentInfo : segments) {

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
    // Execute each merge in the specification synchronously.
    // Used by forceMerge() and commit-time merges. Background merging uses maybeMerge().

    for (const auto& oneMerge : spec->getMerges()) {
        // Resolve segments under lock (segmentInfos_ may be modified by collectNewSegments)
        std::vector<std::shared_ptr<SegmentInfo>> segmentsToMerge;
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            for (auto* segCommit : oneMerge->getSegments()) {
                auto* segInfo = reinterpret_cast<SegmentInfo*>(segCommit);
                for (int i = 0; i < segmentInfos_.size(); i++) {
                    if (segmentInfos_.info(i)->name() == segInfo->name()) {
                        segmentsToMerge.push_back(segmentInfos_.info(i));
                        break;
                    }
                }
            }
        }

        if (segmentsToMerge.size() < 2) {
            continue;
        }

        std::string mergedName = "_merged_" + std::to_string(mergeCounter_.fetch_add(1));

        // Heavy I/O — no lock held
        SegmentMerger merger(directory_, mergedName, segmentsToMerge);
        auto mergedSegment = merger.merge();

        // Compound file — no lock held (just I/O)
        if (useCompoundFile_) {
            auto origFiles = createCompoundFile(mergedSegment);
            for (const auto& file : origFiles) {
                try {
                    directory_.deleteFile(file);
                } catch (const std::exception&) {
                }
            }
        }

        // Update segmentInfos_ under lock
        {
            std::lock_guard<std::mutex> lock(segmentLock_);
            for (int i = segmentInfos_.size() - 1; i >= 0; i--) {
                for (const auto& toMerge : segmentsToMerge) {
                    if (segmentInfos_.info(i)->name() == toMerge->name()) {
                        segmentInfos_.remove(i);
                        break;
                    }
                }
            }
            segmentInfos_.add(mergedSegment);
        }

        // Delete old segment files — no lock needed
        for (const auto& segment : segmentsToMerge) {
            deleteSegmentFiles(segment);
        }
    }
}

}  // namespace index
}  // namespace diagon
