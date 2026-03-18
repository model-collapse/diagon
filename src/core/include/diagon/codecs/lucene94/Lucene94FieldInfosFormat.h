// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/OtherFormats.h"

namespace diagon {
namespace codecs {
namespace lucene94 {

/**
 * Lucene94FieldInfosFormat — writes/reads .fnm files in Lucene 9.4+ format.
 *
 * Wire format:
 *   IndexHeader: codec="Lucene94FieldInfos", version=1, segmentID, suffix=""
 *   FieldCount: VInt
 *   Per field:
 *     FieldName: String
 *     FieldNumber: VInt
 *     FieldBits: byte (storeTermVector|omitNorms|storePayloads|softDeletes|parentField)
 *     IndexOptions: byte (0=NONE, 1=DOCS, 2=DOCS_AND_FREQS, 3=D_F_P, 4=D_F_P_O)
 *     DocValuesType: byte (0=NONE, 1=NUMERIC, 2=BINARY, 3=SORTED, 4=SORTED_NUMERIC, 5=SORTED_SET)
 *     DocValuesGen: int64 (-1)
 *     Attributes: Map<String, String>
 *     PointDimensionCount: VInt
 *     [PointIndexDimensionCount: VInt]  (only if PointDimensionCount > 0)
 *     [PointNumBytes: VInt]              (only if PointDimensionCount > 0)
 *     VectorDimension: VInt (0)
 *     VectorEncoding: byte (0)
 *     VectorSimilarity: byte (0)
 *   Footer
 *
 * Based on: org.apache.lucene.codecs.lucene94.Lucene94FieldInfosFormat
 */
class Lucene94FieldInfosFormat : public FieldInfosFormat {
public:
    static constexpr const char* CODEC_NAME = "Lucene94FieldInfos";
    static constexpr int VERSION_START = 0;
    static constexpr int VERSION_CURRENT = 1;  // VERSION_DOCVALUE_SKIPPER = 1

    // Field bits flags (matching Lucene exactly)
    static constexpr uint8_t STORE_TERMVECTOR = 0x1;
    static constexpr uint8_t OMIT_NORMS = 0x2;
    static constexpr uint8_t STORE_PAYLOADS = 0x4;
    static constexpr uint8_t SOFT_DELETES = 0x8;
    static constexpr uint8_t PARENT_FIELD = 0x10;

    std::string getName() const override { return "Lucene94FieldInfosFormat"; }

    void write(store::Directory& dir, const index::SegmentInfo& si,
               const index::FieldInfos& fieldInfos) override;

    index::FieldInfos read(store::Directory& dir, const index::SegmentInfo& si) override;

    /** Returns the .fnm file name for a segment. */
    static std::string fnmFileName(const std::string& segmentName) {
        return segmentName + ".fnm";
    }
};

}  // namespace lucene94
}  // namespace codecs
}  // namespace diagon
