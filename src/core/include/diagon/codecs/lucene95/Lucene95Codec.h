// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/ColumnFormat.h"
#include "diagon/codecs/DocValuesFormat.h"
#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/OtherFormats.h"
#include "diagon/codecs/PostingsFormat.h"

#include <memory>
#include <string>

namespace diagon {
namespace codecs {
namespace lucene95 {

/**
 * Lucene95Codec — read-only codec for OpenSearch 2.11 indices.
 *
 * OpenSearch 2.11 uses Lucene 9.7 internally, which registers as "Lucene95".
 * This codec delegates to format readers that can parse the Lucene 9.x wire formats:
 *
 *   - SegmentInfoFormat:   Lucene90SegmentInfo v0  (compatible with Lucene99SegmentInfoFormat)
 *   - FieldInfosFormat:    Lucene94FieldInfos v0   (compatible with Lucene94FieldInfosFormat)
 *   - StoredFieldsFormat:  Lucene90StoredFieldsFastData (compatible with Lucene90OSStoredFieldsReader)
 *   - PostingsFormat:      Lucene90PostingsFormat (128-block PFOR, BlockTree FST)
 *   - NormsFormat:         Lucene90NormsFormat — NOT YET IMPLEMENTED
 *   - DocValuesFormat:     Lucene90DocValuesFormat — NOT YET IMPLEMENTED
 *   - PointsFormat:        Lucene90PointsFormat — NOT YET IMPLEMENTED
 *   - CompoundFormat:      Lucene90CompoundFormat (CodecUtil headers) — PARTIAL
 *
 * See: design/16_OPENSEARCH_FORMAT_COMPATIBILITY.md
 */
class Lucene95Codec : public Codec {
public:
    Lucene95Codec();

    // ==================== Format Accessors ====================

    PostingsFormat& postingsFormat() override { return *postingsFormat_; }
    DocValuesFormat& docValuesFormat() override { return *docValuesFormat_; }
    ColumnFormat& columnFormat() override { return *columnFormat_; }
    StoredFieldsFormat& storedFieldsFormat() override { return *storedFieldsFormat_; }
    TermVectorsFormat& termVectorsFormat() override { return *termVectorsFormat_; }
    FieldInfosFormat& fieldInfosFormat() override { return *fieldInfosFormat_; }
    SegmentInfoFormat& segmentInfoFormat() override { return *segmentInfoFormat_; }
    NormsFormat& normsFormat() override { return *normsFormat_; }
    LiveDocsFormat& liveDocsFormat() override { return *liveDocsFormat_; }
    PointsFormat& pointsFormat() override { return *pointsFormat_; }
    VectorFormat& vectorFormat() override { return *vectorFormat_; }

    // ==================== Identification ====================

    /** Returns "Lucene95" — the codec name OpenSearch 2.11 uses. */
    std::string getName() const override { return "Lucene95"; }

    // ==================== Capabilities ====================

    uint64_t getCapabilities() const override {
        // Read-only codec for OS indices — supports what we can currently read.
        return static_cast<uint64_t>(Capability::POSTINGS);
    }

private:
    std::unique_ptr<PostingsFormat> postingsFormat_;
    std::unique_ptr<DocValuesFormat> docValuesFormat_;
    std::unique_ptr<ColumnFormat> columnFormat_;
    std::unique_ptr<StoredFieldsFormat> storedFieldsFormat_;
    std::unique_ptr<TermVectorsFormat> termVectorsFormat_;
    std::unique_ptr<FieldInfosFormat> fieldInfosFormat_;
    std::unique_ptr<SegmentInfoFormat> segmentInfoFormat_;
    std::unique_ptr<NormsFormat> normsFormat_;
    std::unique_ptr<LiveDocsFormat> liveDocsFormat_;
    std::unique_ptr<PointsFormat> pointsFormat_;
    std::unique_ptr<VectorFormat> vectorFormat_;
};

}  // namespace lucene95
}  // namespace codecs
}  // namespace diagon
