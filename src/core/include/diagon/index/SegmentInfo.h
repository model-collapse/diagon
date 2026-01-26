// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace diagon {

// Forward declarations
namespace store {
class Directory;
}

namespace index {

/**
 * SegmentInfo - Metadata for a single segment
 *
 * Based on: org.apache.lucene.index.SegmentInfo
 *
 * Holds all metadata about a segment:
 * - Segment name (e.g., "_0", "_1")
 * - Document count (maxDoc)
 * - Codec name
 * - List of files belonging to segment
 * - Diagnostics (arbitrary key-value metadata)
 *
 * Phase 3 Design:
 * - Simple in-memory representation
 * - No file I/O (deferred to Phase 4)
 * - Tracks basic metadata only
 *
 * Phase 4 Enhancement:
 * - Write to/read from .si files
 * - Integration with Directory and Codec
 * - Version tracking and checksums
 *
 * Immutable after creation (except file list updates).
 */
class SegmentInfo {
public:
    /**
     * Constructor
     *
     * @param name Segment name (e.g., "_0")
     * @param maxDoc Number of documents in segment
     * @param codecName Codec name used for this segment
     */
    SegmentInfo(const std::string& name, int maxDoc, const std::string& codecName = "Lucene104");

    /**
     * Get segment name
     */
    const std::string& name() const { return name_; }

    /**
     * Get maximum document ID (= document count, including deleted)
     */
    int maxDoc() const { return maxDoc_; }

    /**
     * Get number of deleted documents
     * Phase 3: Track deletions
     */
    int delCount() const { return delCount_; }

    /**
     * Set number of deleted documents
     * Phase 3: Track deletions
     */
    void setDelCount(int delCount) { delCount_ = delCount; }

    /**
     * Check if segment has deletions
     */
    bool hasDeletions() const { return delCount_ > 0; }

    /**
     * Get codec name
     */
    const std::string& codecName() const { return codecName_; }

    /**
     * Get list of files
     */
    const std::vector<std::string>& files() const { return files_; }

    /**
     * Add file to segment
     *
     * @param fileName File name to add
     */
    void addFile(const std::string& fileName);

    /**
     * Set files list
     *
     * @param files Vector of file names
     */
    void setFiles(const std::vector<std::string>& files);

    /**
     * Get diagnostics
     */
    const std::map<std::string, std::string>& diagnostics() const { return diagnostics_; }

    /**
     * Set diagnostic value
     *
     * @param key Diagnostic key
     * @param value Diagnostic value
     */
    void setDiagnostic(const std::string& key, const std::string& value);

    /**
     * Get diagnostic value
     *
     * @param key Diagnostic key
     * @return Value if exists, empty string otherwise
     */
    std::string getDiagnostic(const std::string& key) const;

    /**
     * Get total size of files (bytes)
     * Phase 3: Returns 0 (no actual files yet)
     */
    int64_t sizeInBytes() const { return sizeInBytes_; }

    /**
     * Set size in bytes
     * Phase 3: For testing/tracking
     */
    void setSizeInBytes(int64_t size) { sizeInBytes_ = size; }

    /**
     * Get field infos
     * Phase 4: Store FieldInfos directly in SegmentInfo
     */
    const FieldInfos& fieldInfos() const { return fieldInfos_; }

    /**
     * Set field infos
     * Phase 4: Allow setting FieldInfos
     */
    void setFieldInfos(FieldInfos&& fieldInfos) { fieldInfos_ = std::move(fieldInfos); }

private:
    std::string name_;                                // Segment name
    int maxDoc_;                                      // Document count (including deleted)
    int delCount_{0};                                 // Number of deleted documents
    std::string codecName_;                           // Codec name
    std::vector<std::string> files_;                  // Files in segment
    std::map<std::string, std::string> diagnostics_;  // Diagnostics
    int64_t sizeInBytes_{0};                          // Total size
    FieldInfos fieldInfos_;                           // Field metadata (Phase 4)
};

/**
 * SegmentInfos - Collection of segment metadata
 *
 * Based on: org.apache.lucene.index.SegmentInfos
 *
 * Represents the segments_N file that lists all segments in the index.
 * Tracks:
 * - Version/generation counter
 * - List of SegmentInfo objects
 * - Index metadata
 *
 * Phase 3 Design:
 * - In-memory only
 * - Simple list of segments
 * - Generation counter
 *
 * Phase 4 Enhancement:
 * - Read/write segments_N files
 * - Commit protocol
 * - Generation management
 *
 * Thread Safety: NOT thread-safe, caller must synchronize.
 */
class SegmentInfos {
public:
    /**
     * Constructor
     */
    SegmentInfos();

    /**
     * Add segment to collection
     *
     * @param segmentInfo Segment to add
     */
    void add(std::shared_ptr<SegmentInfo> segmentInfo);

    /**
     * Get number of segments
     */
    int size() const { return static_cast<int>(segments_.size()); }

    /**
     * Get segment by index
     *
     * @param index Segment index
     * @return SegmentInfo pointer
     */
    std::shared_ptr<SegmentInfo> info(int index) const;

    /**
     * Get all segments
     */
    const std::vector<std::shared_ptr<SegmentInfo>>& segments() const { return segments_; }

    /**
     * Get total document count across all segments
     */
    int totalMaxDoc() const;

    /**
     * Get generation (version counter)
     *
     * Incremented on each commit.
     * Format: segments_N where N is generation in base-36.
     */
    int64_t getGeneration() const { return generation_; }

    /**
     * Increment generation
     * Called when committing changes.
     */
    void incrementGeneration() { generation_++; }

    /**
     * Get version
     * Tracks modification count.
     */
    int64_t getVersion() const { return version_; }

    /**
     * Increment version
     */
    void incrementVersion() { version_++; }

    /**
     * Clear all segments
     */
    void clear();

    /**
     * Get segments_N file name
     *
     * @param generation Generation number
     * @return File name (e.g., "segments_1", "segments_a")
     */
    static std::string getSegmentsFileName(int64_t generation);

    /**
     * Remove segment at index
     *
     * @param index Segment index to remove
     */
    void remove(int index);

    // ==================== Phase 4: Read segments_N ====================

    /**
     * Read segments_N file from directory
     *
     * @param dir Directory containing index
     * @param fileName segments_N file name
     * @return SegmentInfos loaded from file
     * @throws IOException if file doesn't exist or is corrupted
     */
    static SegmentInfos read(store::Directory& dir, const std::string& fileName);

    /**
     * Read latest commit from directory
     *
     * Finds the highest generation segments_N file and reads it.
     *
     * @param dir Directory containing index
     * @return SegmentInfos loaded from latest commit
     * @throws IOException if no segments file found
     */
    static SegmentInfos readLatestCommit(store::Directory& dir);

    /**
     * Find maximum generation number in directory
     *
     * @param dir Directory to scan
     * @return Max generation, or -1 if no segments files
     */
    static int64_t findMaxGeneration(store::Directory& dir);

private:
    std::vector<std::shared_ptr<SegmentInfo>> segments_;  // Segment list
    int64_t generation_{0};                               // Generation counter
    int64_t version_{0};                                  // Version counter
};

}  // namespace index
}  // namespace diagon
