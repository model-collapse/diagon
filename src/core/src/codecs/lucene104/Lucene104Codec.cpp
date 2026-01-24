// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104Codec.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

Lucene104Codec::Lucene104Codec()
    : postingsFormat_(std::make_unique<Lucene104PostingsFormat>())
    , docValuesFormat_(std::make_unique<Lucene104DocValuesFormat>())
    , columnFormat_(std::make_unique<MergeTreeColumnFormat>())
    , storedFieldsFormat_(std::make_unique<Lucene104StoredFieldsFormat>())
    , termVectorsFormat_(std::make_unique<Lucene104TermVectorsFormat>())
    , fieldInfosFormat_(std::make_unique<Lucene104FieldInfosFormat>())
    , segmentInfoFormat_(std::make_unique<Lucene104SegmentInfoFormat>())
    , normsFormat_(std::make_unique<Lucene104NormsFormat>())
    , liveDocsFormat_(std::make_unique<Lucene104LiveDocsFormat>())
    , pointsFormat_(std::make_unique<Lucene104PointsFormat>())
    , vectorFormat_(std::make_unique<Lucene104VectorFormat>()) {}

// Register at startup
namespace {
struct Lucene104CodecRegistrar {
    Lucene104CodecRegistrar() {
        Codec::registerCodec("Lucene104", []() {
            return std::make_unique<Lucene104Codec>();
        });
    }
};

// Static initialization ensures registration before main()
static Lucene104CodecRegistrar g_lucene104CodecRegistrar;
}  // anonymous namespace

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
