// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104Codec.h"
#include "diagon/codecs/lucene104/Lucene104FieldsConsumer.h"
#include "diagon/codecs/lucene104/Lucene104NormsWriter.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== Lucene104PostingsFormat Implementation ====================

std::unique_ptr<FieldsConsumer> Lucene104PostingsFormat::fieldsConsumer(index::SegmentWriteState& state) {
    return std::make_unique<Lucene104FieldsConsumer>(state);
}

std::unique_ptr<FieldsProducer> Lucene104PostingsFormat::fieldsProducer(index::SegmentReadState& state) {
    // TODO: Implement FieldsProducer that uses Lucene104PostingsReader
    return nullptr;
}

// ==================== Lucene104Codec Implementation ====================

Lucene104Codec::Lucene104Codec()
    : postingsFormat_(std::make_unique<Lucene104PostingsFormat>())
    , docValuesFormat_(std::make_unique<Lucene104DocValuesFormat>())
    , columnFormat_(std::make_unique<MergeTreeColumnFormat>())
    , storedFieldsFormat_(std::make_unique<Lucene104StoredFieldsFormat>())
    , termVectorsFormat_(std::make_unique<Lucene104TermVectorsFormat>())
    , fieldInfosFormat_(std::make_unique<Lucene104FieldInfosFormat>())
    , segmentInfoFormat_(std::make_unique<Lucene104SegmentInfoFormat>())
    , normsFormat_(std::make_unique<Lucene104NormsFormat>())
    , liveDocsFormat_(std::make_unique<LiveDocsFormat>())
    , pointsFormat_(std::make_unique<Lucene104PointsFormat>())
    , vectorFormat_(std::make_unique<Lucene104VectorFormat>()) {}

// Register at startup
namespace {
struct Lucene104CodecRegistrar {
    Lucene104CodecRegistrar() {
        Codec::registerCodec("Lucene104", []() { return std::make_unique<Lucene104Codec>(); });
    }
};

// Static initialization ensures registration before main()
static Lucene104CodecRegistrar g_lucene104CodecRegistrar;
}  // anonymous namespace

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
