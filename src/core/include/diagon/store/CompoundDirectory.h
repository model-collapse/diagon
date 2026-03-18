// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/Directory.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon::store {

/**
 * @brief Read-only Directory that provides access to files packed in a compound file (.cfs/.cfe).
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90CompoundReader
 *           org.apache.lucene.codecs.CompoundDirectory
 *
 * Reads the .cfe entry table to build an in-memory map of {filename -> (offset, length)},
 * then provides openInput() that returns a slice of the .cfs data file.
 *
 * Write operations throw UnsupportedOperationException (read-only directory).
 *
 * Usage:
 * ```cpp
 * auto cfsDir = CompoundDirectory::open(dir, "_0");
 * auto input = cfsDir->openInput("_0.doc", IOContext::READ);
 * // Read data from the compound file slice...
 * ```
 */
class CompoundDirectory : public Directory {
public:
    /** Entry in the compound file: offset and length within the .cfs file */
    struct FileEntry {
        int64_t offset;
        int64_t length;
    };

    /**
     * @brief Open a compound directory for reading.
     *
     * Reads the .cfe entry table and opens a handle to the .cfs data file.
     *
     * @param dir Underlying directory containing the .cfs and .cfe files
     * @param segmentName Segment name (e.g., "_0")
     * @return CompoundDirectory instance
     * @throws IOException if .cfs or .cfe files don't exist or are corrupted
     */
    static std::unique_ptr<CompoundDirectory> open(Directory& dir, const std::string& segmentName);

    /**
     * @brief Construct CompoundDirectory.
     *
     * @param dir Underlying directory
     * @param segmentName Segment name
     * @param entries Map of stripped filename -> FileEntry
     * @param handle IndexInput for the .cfs data file
     */
    CompoundDirectory(Directory& dir, const std::string& segmentName,
                      std::unordered_map<std::string, FileEntry> entries,
                      std::unique_ptr<IndexInput> handle);

    ~CompoundDirectory() override;

    // ==================== Read Operations ====================

    std::vector<std::string> listAll() const override;
    int64_t fileLength(const std::string& name) const override;
    std::unique_ptr<IndexInput> openInput(const std::string& name,
                                          const IOContext& context) const override;

    // ==================== Write Operations (Unsupported) ====================

    void deleteFile(const std::string& name) override;
    std::unique_ptr<IndexOutput> createOutput(const std::string& name,
                                              const IOContext& context) override;
    std::unique_ptr<IndexOutput> createTempOutput(const std::string& prefix,
                                                  const std::string& suffix,
                                                  const IOContext& context) override;
    void rename(const std::string& source, const std::string& dest) override;
    void sync(const std::vector<std::string>& names) override;
    void syncMetaData() override;
    std::unique_ptr<Lock> obtainLock(const std::string& name) override;

    // ==================== Lifecycle ====================

    void close() override;

    // ==================== Utilities ====================

    std::string toString() const override;

    /**
     * @brief Get the number of files in this compound directory.
     */
    size_t numFiles() const { return entries_.size(); }

private:
    Directory& directory_;
    std::string segmentName_;
    std::unordered_map<std::string, FileEntry> entries_;
    std::unique_ptr<IndexInput> handle_;

    /**
     * @brief Strip segment name prefix from filename.
     */
    static std::string stripSegmentName(const std::string& fileName,
                                        const std::string& segmentName);

    /**
     * @brief Read entries from .cfe file (auto-detects Diagon native vs Lucene format).
     */
    static std::unordered_map<std::string, FileEntry> readEntries(Directory& dir,
                                                                  const std::string& entriesFile);

    /** Read entries in Diagon native format (VInt count + entries, no headers). */
    static std::unordered_map<std::string, FileEntry> readEntriesNative(IndexInput& input);

    /** Read entries in Lucene90 format (CodecUtil header + entries + footer). */
    static std::unordered_map<std::string, FileEntry> readEntriesLucene(IndexInput& input);
};

}  // namespace diagon::store
