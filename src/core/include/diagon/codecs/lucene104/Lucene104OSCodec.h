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
namespace lucene104 {

/**
 * OpenSearch-compatible codec implementation.
 *
 * Registered as "Lucene104" — the codec name OpenSearch expects to see
 * in segments_N files. Produces index files that are byte-level compatible
 * with OpenSearch/Lucene.
 *
 * Initially delegates to the same native format implementations as
 * Lucene104Codec (the Diagon104 codec). OS-compat format writers will be
 * plugged in by subsequent tasks as they are implemented.
 *
 * See: design/16_OPENSEARCH_FORMAT_COMPATIBILITY.md
 */
class Lucene104OSCodec : public Codec {
public:
    Lucene104OSCodec();

    // ==================== Format Accessors ====================
    // Initially delegate to native format implementations.
    // OS-compat format writers will replace these as implemented.

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

    /** Returns "Lucene104" — the codec name OpenSearch expects. */
    std::string getName() const override { return "Lucene104"; }

    // ==================== Capabilities ====================

    uint64_t getCapabilities() const override {
        // Same capabilities as native codec (format implementations are shared initially)
        return static_cast<uint64_t>(Capability::POSTINGS) |
               static_cast<uint64_t>(Capability::DOC_VALUES) |
               static_cast<uint64_t>(Capability::COLUMN_STORAGE) |
               static_cast<uint64_t>(Capability::SKIP_INDEXES) |
               static_cast<uint64_t>(Capability::SIMD_ACCELERATION) |
               static_cast<uint64_t>(Capability::COMPRESSION_ZSTD) |
               static_cast<uint64_t>(Capability::ADAPTIVE_GRANULES);
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

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
