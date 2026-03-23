// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/Lucene90PostingsFormat.h"

#include "diagon/codecs/lucene90/Lucene90BlockTreeTermsReader.h"
#include "diagon/codecs/lucene90/Lucene90PostingsReader.h"

#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene90 {

std::unique_ptr<FieldsConsumer>
Lucene90PostingsFormat::fieldsConsumer(index::SegmentWriteState& /*state*/) {
    throw std::runtime_error(
        "Lucene90PostingsFormat: write not supported (read-only codec for OS indices)");
}

std::unique_ptr<FieldsProducer>
Lucene90PostingsFormat::fieldsProducer(index::SegmentReadState& state) {
    auto postingsReader = std::make_unique<Lucene90PostingsReader>(state);
    return std::make_unique<Lucene90BlockTreeTermsReader>(state, std::move(postingsReader));
}

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
