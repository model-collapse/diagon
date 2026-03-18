// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104OSCodec.h"

#include "diagon/codecs/lucene104/Lucene104Codec.h"
#include "diagon/codecs/lucene104/Lucene104NormsWriter.h"
#include "diagon/codecs/lucene104/Lucene104OSPostingsFormat.h"
#include "diagon/codecs/lucene94/Lucene94FieldInfosFormat.h"
#include "diagon/codecs/lucene99/Lucene99SegmentInfoFormat.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== Lucene104OSCodec Implementation ====================

// Initially uses the same format implementations as the native Diagon104 codec.
// OS-compat format writers (CodecUtil headers/footers, ForUtil encoding, etc.)
// will be plugged in as subsequent tasks complete them.

Lucene104OSCodec::Lucene104OSCodec()
    : postingsFormat_(std::make_unique<Lucene104PostingsFormat>())
    , docValuesFormat_(std::make_unique<Lucene104DocValuesFormat>())
    , columnFormat_(std::make_unique<MergeTreeColumnFormat>())
    , storedFieldsFormat_(std::make_unique<Lucene104StoredFieldsFormat>())
    , termVectorsFormat_(std::make_unique<Lucene104TermVectorsFormat>())
    , fieldInfosFormat_(std::make_unique<lucene94::Lucene94FieldInfosFormat>())
    , segmentInfoFormat_(std::make_unique<lucene99::Lucene99SegmentInfoFormat>())
    , normsFormat_(std::make_unique<Lucene104NormsFormat>())
    , liveDocsFormat_(std::make_unique<LiveDocsFormat>())
    , pointsFormat_(std::make_unique<Lucene104PointsFormat>())
    , vectorFormat_(std::make_unique<Lucene104VectorFormat>()) {}

// Register as "Lucene104" — the name OpenSearch expects in segments_N
namespace {
struct Lucene104OSCodecRegistrar {
    Lucene104OSCodecRegistrar() {
        Codec::registerCodec("Lucene104", []() { return std::make_unique<Lucene104OSCodec>(); });
    }
};

static Lucene104OSCodecRegistrar g_lucene104OSCodecRegistrar;
}  // anonymous namespace

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
