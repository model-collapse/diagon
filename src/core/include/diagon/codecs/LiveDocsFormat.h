// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/Directory.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/BitSet.h"
#include "diagon/util/Bits.h"

#include <memory>
#include <string>

namespace diagon {
namespace codecs {

/**
 * LiveDocsFormat - Read and write live docs (deleted documents bitmap)
 *
 * Based on: org.apache.lucene.codecs.LiveDocsFormat
 *
 * File Format (.liv):
 * - Header (codec name, version)
 * - numDocs (int32)
 * - delCount (int32) - number of deleted documents
 * - bits[] (uint64 array) - bitset words (1 = live, 0 = deleted)
 *
 * Design:
 * - Optional file - only created when deletions exist
 * - Simple dense format (always uses FixedBitSet)
 * - bit=1 means document is LIVE
 * - bit=0 means document is DELETED
 *
 * Usage:
 *   // Write live docs
 *   LiveDocsFormat format;
 *   BitSet liveDocs(maxDoc);
 *   // ... set bits ...
 *   format.writeLiveDocs(directory, segmentName, liveDocs, delCount);
 *
 *   // Read live docs
 *   auto liveDocs = format.readLiveDocs(directory, segmentName, maxDoc);
 *
 * Thread Safety:
 * - Thread-safe (stateless)
 */
class LiveDocsFormat {
public:
    /**
     * Constructor
     */
    LiveDocsFormat() = default;

    /**
     * Write live docs to .liv file
     *
     * @param directory Directory to write to
     * @param segmentName Segment name (e.g., "_0")
     * @param liveDocs Bitset of live documents (1 = live, 0 = deleted)
     * @param delCount Number of deleted documents
     */
    void writeLiveDocs(store::Directory& directory, const std::string& segmentName,
                       const util::BitSet& liveDocs, int delCount);

    /**
     * Read live docs from .liv file
     *
     * @param directory Directory to read from
     * @param segmentName Segment name (e.g., "_0")
     * @param maxDoc Maximum document count
     * @return BitSet of live documents, or nullptr if no deletions
     */
    std::unique_ptr<util::BitSet> readLiveDocs(store::Directory& directory,
                                               const std::string& segmentName, int maxDoc);

    /**
     * Check if live docs file exists
     *
     * @param directory Directory to check
     * @param segmentName Segment name
     * @return true if .liv file exists
     */
    bool liveDocsExist(store::Directory& directory, const std::string& segmentName);

private:
    static constexpr const char* CODEC_NAME = "DiagonLiveDocs";
    static constexpr int VERSION = 1;
    static constexpr const char* EXTENSION = ".liv";
};

}  // namespace codecs
}  // namespace diagon
