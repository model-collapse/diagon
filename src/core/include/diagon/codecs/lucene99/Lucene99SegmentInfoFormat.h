// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/OtherFormats.h"

namespace diagon {
namespace codecs {
namespace lucene99 {

/**
 * Lucene99SegmentInfoFormat — writes/reads .si files in Lucene 9.x format.
 *
 * Wire format:
 *   IndexHeader: codec="Lucene90SegmentInfo", version=3, segmentID, suffix=""
 *   LuceneVersion: major(VInt) minor(VInt) bugfix(VInt)
 *   HasMinVersion: byte(1)
 *   MinVersion: major(VInt) minor(VInt) bugfix(VInt)
 *   DocCount: int32
 *   IsCompoundFile: byte (0 or 1)
 *   HasBlocks: byte(0)
 *   Diagnostics: Map<String, String>
 *   Files: Set<String>
 *   Attributes: Map<String, String>
 *   SortFields: VInt(0)   [no index sorting]
 *   Footer
 *
 * Based on: org.apache.lucene.codecs.lucene99.Lucene99SegmentInfoFormat
 */
class Lucene99SegmentInfoFormat : public SegmentInfoFormat {
public:
    static constexpr const char* CODEC_NAME = "Lucene90SegmentInfo";
    static constexpr int VERSION_START = 0;
    static constexpr int VERSION_CURRENT = 3;

    // Lucene version we claim to be (9.12.0 — recent stable)
    static constexpr int LUCENE_VERSION_MAJOR = 9;
    static constexpr int LUCENE_VERSION_MINOR = 12;
    static constexpr int LUCENE_VERSION_BUGFIX = 0;

    std::string getName() const override { return "Lucene99SegmentInfoFormat"; }

    void write(store::Directory& dir, const index::SegmentInfo& si) override;

    std::shared_ptr<index::SegmentInfo> read(store::Directory& dir,
                                              const std::string& segmentName,
                                              const uint8_t* segmentID) override;

    /** Returns the .si file name for a segment. */
    static std::string siFileName(const std::string& segmentName) {
        return segmentName + ".si";
    }
};

}  // namespace lucene99
}  // namespace codecs
}  // namespace diagon
