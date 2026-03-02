// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/Directory.h"

#include <string>
#include <vector>

namespace diagon::store {

/**
 * @brief Writes a compound file (.cfs) and its entry table (.cfe).
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90CompoundFormat
 *
 * Packs all per-segment files into a single .cfs data file with a .cfe
 * entry index. This reduces filesystem metadata overhead when segments
 * have many small files (~11 per segment).
 *
 * File format:
 *   .cfs (data):    [Header] [FileData_1 (aligned)] ... [FileData_N (aligned)]
 *   .cfe (entries): [NumEntries(VInt)] [FileName(String) Offset(Long) Length(Long)] ...
 *
 * Notes:
 *   - File data is aligned to 64 bytes for mmap-friendly access
 *   - Files are sorted by size (smallest first) so small files pack into one page
 *   - Segment name prefix is stripped from entry filenames for compactness
 */
class CompoundFileWriter {
public:
    /** Alignment boundary for file data in .cfs (matches Lucene's 64-byte alignment) */
    static constexpr int ALIGNMENT_BYTES = 64;

    /** File extensions */
    static constexpr const char* DATA_EXTENSION = ".cfs";
    static constexpr const char* ENTRIES_EXTENSION = ".cfe";

    /**
     * @brief Write compound file for a segment.
     *
     * Reads all files in fileNames from dir, packs them into a single .cfs file,
     * and writes a .cfe entry table. The original files are NOT deleted.
     *
     * @param dir Directory containing the segment files (also where .cfs/.cfe are written)
     * @param segmentName Segment name (e.g., "_0") used as filename prefix
     * @param fileNames List of filenames to pack into the compound file
     * @throws IOException on I/O error
     */
    static void write(Directory& dir, const std::string& segmentName,
                      const std::vector<std::string>& fileNames);

    /**
     * @brief Get the .cfs data filename for a segment.
     * @param segmentName Segment name
     * @return e.g., "_0.cfs"
     */
    static std::string getDataFileName(const std::string& segmentName);

    /**
     * @brief Get the .cfe entries filename for a segment.
     * @param segmentName Segment name
     * @return e.g., "_0.cfe"
     */
    static std::string getEntriesFileName(const std::string& segmentName);

private:
    /**
     * @brief Strip segment name prefix from a filename.
     *
     * e.g., "_0.doc" with segment "_0" -> ".doc"
     *
     * @param fileName Full filename
     * @param segmentName Segment name prefix
     * @return Filename with segment prefix removed
     */
    static std::string stripSegmentName(const std::string& fileName,
                                        const std::string& segmentName);
};

}  // namespace diagon::store
