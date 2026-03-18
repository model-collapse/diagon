// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104OSPostingsFormat.h"

#include "diagon/codecs/lucene104/Lucene104FieldsConsumer.h"
#include "diagon/codecs/lucene104/Lucene104FieldsProducer.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

std::unique_ptr<FieldsConsumer> Lucene104OSPostingsFormat::fieldsConsumer(
    index::SegmentWriteState& state) {
    // For now, delegate to the native FieldsConsumer
    // The OS-compat postings writer will be integrated when the full
    // term dictionary (BlockTreeTermsWriter) is updated to use it.
    // The key files (.doc/.pos) are written by Lucene104OSPostingsWriter
    // which produces the correct wire format.
    return std::make_unique<Lucene104FieldsConsumer>(state);
}

std::unique_ptr<FieldsProducer> Lucene104OSPostingsFormat::fieldsProducer(
    index::SegmentReadState& state) {
    // Delegate to native reader for now
    return std::make_unique<Lucene104FieldsProducer>(state);
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
