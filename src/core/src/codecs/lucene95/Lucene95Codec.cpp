// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene95/Lucene95Codec.h"

#include "diagon/codecs/lucene104/Lucene104Codec.h"  // MergeTreeColumnFormat
#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsFormat.h"
#include "diagon/codecs/lucene90/Lucene90PostingsFormat.h"
#include "diagon/codecs/lucene94/Lucene94FieldInfosFormat.h"
#include "diagon/codecs/lucene95/Lucene95Stubs.h"
#include "diagon/codecs/lucene99/Lucene99SegmentInfoFormat.h"

namespace diagon {
namespace codecs {
namespace lucene95 {

// ==================== Lucene95Codec Implementation ====================

// Read-only codec for OpenSearch 2.11 (Lucene 9.7) indices.
// Compatible formats are wired to real readers; incompatible formats use stubs.

Lucene95Codec::Lucene95Codec()
    : postingsFormat_(std::make_unique<lucene90::Lucene90PostingsFormat>())
    , docValuesFormat_(std::make_unique<Lucene90DocValuesFormatStub>())
    , columnFormat_(std::make_unique<lucene104::MergeTreeColumnFormat>())
    , storedFieldsFormat_(std::make_unique<lucene90::Lucene90OSStoredFieldsFormat>())
    , termVectorsFormat_(std::make_unique<Lucene90TermVectorsFormatStub>())
    , fieldInfosFormat_(std::make_unique<lucene94::Lucene94FieldInfosFormat>())
    , segmentInfoFormat_(std::make_unique<lucene99::Lucene99SegmentInfoFormat>())
    , normsFormat_(std::make_unique<Lucene90NormsFormatStub>())
    , liveDocsFormat_(std::make_unique<LiveDocsFormat>())
    , pointsFormat_(std::make_unique<Lucene90PointsFormatStub>())
    , vectorFormat_(std::make_unique<Lucene95HnswVectorsFormatStub>()) {}

// Register as "Lucene95" — the codec name OpenSearch 2.11 writes into segments_N
namespace {
struct Lucene95CodecRegistrar {
    Lucene95CodecRegistrar() {
        Codec::registerCodec("Lucene95", []() { return std::make_unique<Lucene95Codec>(); });
    }
};

static Lucene95CodecRegistrar g_lucene95CodecRegistrar;
}  // anonymous namespace

}  // namespace lucene95
}  // namespace codecs
}  // namespace diagon
